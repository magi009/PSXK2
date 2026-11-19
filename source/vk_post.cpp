// Posprocesado Vulkan: la imagen del juego se dibuja con un pipeline propio.
//  - Copia directa o FXAA en la pasada final (directo a pantalla, a resolución de salida).
//  - TAA ligero: una pasada previa a resolución del juego que mezcla con el frame anterior,
//    recortando el historial al vecindario actual (sin estelas). FXAA + TAA aplica FXAA al frame actual.
#include "vk_presenter.h"
#include "log.h"
#include <cstdint>
#include "spv_fullscreen_vert.h"
#include "spv_present_frag.h"
#include "spv_taa_frag.h"

struct PostPC { float rcp[2]; float mode; float blend; };

static VkShaderModule makeModule(VkDevice dev, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes; ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(dev, &ci, nullptr, &m);
    return m;
}

static VkPipeline makePipeline(VkDevice dev, VkRenderPass rp, VkPipelineLayout layout, VkShaderModule vs, VkShaderModule fs) {
    VkPipelineShaderStageCreateInfo st[2] = {};
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba = {};
    ba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &ba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount = 2; pi.pStages = st; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp; pi.pRasterizationState = &rs; pi.pMultisampleState = &ms;
    pi.pColorBlendState = &cb; pi.pDynamicState = &ds; pi.layout = layout; pi.renderPass = rp;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pi, nullptr, &p) != VK_SUCCESS) return VK_NULL_HANDLE;
    return p;
}

bool VkPresenter::ensurePost() {
    if (postFailed_ || !uiReady_ || !uiPass_) return false;
    if (postReady_ && postForPass_ == uiPass_) return true;
    if (postReady_) {   // el render pass de la pantalla cambió (formato nuevo): solo se rehace ese pipeline
        vkDestroyPipeline(dev_, pipePresent_, nullptr);
        pipePresent_ = makePipeline(dev_, uiPass_, postLayout_, vsMod_, fsPresent_);
        postForPass_ = uiPass_;
        return pipePresent_ != VK_NULL_HANDLE;
    }
    VkDescriptorSetLayoutBinding b[2] = {};
    for (int i = 0; i < 2; i++) {
        b[i].binding = i; b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2; dl.pBindings = b;
    vkCreateDescriptorSetLayout(dev_, &dl, nullptr, &postDsl_);
    VkPushConstantRange pr = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPC)};
    VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &postDsl_; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pr;
    vkCreatePipelineLayout(dev_, &pl, nullptr, &postLayout_);
    for (auto& pool : postPool_) {
        VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32};
        VkDescriptorPoolCreateInfo dp = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = 16; dp.poolSizeCount = 1; dp.pPoolSizes = &ps;
        vkCreateDescriptorPool(dev_, &dp, nullptr, &pool);
    }
    // Render pass del historial TAA: se sobrescribe entero y queda listo para leerse
    VkAttachmentDescription att = {};
    att.format = VK_FORMAT_R8G8B8A8_UNORM; att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
    VkSubpassDependency dep[2] = {};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL; dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0; dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpi = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1; rpi.pAttachments = &att; rpi.subpassCount = 1; rpi.pSubpasses = &sub;
    rpi.dependencyCount = 2; rpi.pDependencies = dep;
    vkCreateRenderPass(dev_, &rpi, nullptr, &histPass_);

    vsMod_ = makeModule(dev_, spv_fullscreen_vert, sizeof(spv_fullscreen_vert));
    fsPresent_ = makeModule(dev_, spv_present_frag, sizeof(spv_present_frag));
    fsTaa_ = makeModule(dev_, spv_taa_frag, sizeof(spv_taa_frag));
    pipePresent_ = makePipeline(dev_, uiPass_, postLayout_, vsMod_, fsPresent_);
    pipeTaa_ = makePipeline(dev_, histPass_, postLayout_, vsMod_, fsTaa_);
    postForPass_ = uiPass_;
    postReady_ = pipePresent_ && pipeTaa_;
    if (!postReady_) { postFailed_ = true; LOGI("[vk] no se pudo crear el posprocesado; se usa el modo simple"); destroyPost(); }
    else LOGI("[vk] posprocesado listo (FXAA / TAA)");
    return postReady_;
}

void VkPresenter::collectHistory() {
    for (size_t i = 0; i < histGrave_.size();) {
        if (frameCount_ >= histGrave_[i].freeAt) {
            Hist& h = histGrave_[i];
            vkDestroyFramebuffer(dev_, h.fb, nullptr); vkDestroyImageView(dev_, h.view, nullptr);
            vkDestroyImage(dev_, h.img, nullptr); vkFreeMemory(dev_, h.mem, nullptr);
            histGrave_.erase(histGrave_.begin() + i);
        } else i++;
    }
}

bool VkPresenter::ensureHistory(uint32_t w, uint32_t h) {
    if (hist_[0].img && histW_ == w && histH_ == h) return true;
    // Las anteriores se liberan cuando ningún frame en vuelo las use (sin detener la GPU)
    for (auto& old : hist_) if (old.img) { old.freeAt = frameCount_ + kFrames + 2; histGrave_.push_back(old); old = {}; }
    for (auto& hh : hist_) {
        VkImageCreateInfo ii = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_UNORM; ii.extent = {w, h, 1};
        ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (vkCreateImage(dev_, &ii, nullptr, &hh.img) != VK_SUCCESS) return false;
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev_, hh.img, &mr);
        VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = mr.size; mai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(dev_, &mai, nullptr, &hh.mem);
        vkBindImageMemory(dev_, hh.img, hh.mem, 0);
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = hh.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(dev_, &vi, nullptr, &hh.view);
        VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = histPass_; fi.attachmentCount = 1; fi.pAttachments = &hh.view;
        fi.width = w; fi.height = h; fi.layers = 1;
        vkCreateFramebuffer(dev_, &fi, nullptr, &hh.fb);
    }
    histW_ = w; histH_ = h; histValid_ = false;
    LOGI("[vk] historial TAA %ux%u", w, h);
    return true;
}

VkDescriptorSet VkPresenter::allocPostSet(VkImageView a, VkImageLayout la, VkImageView b, VkImageLayout lb, VkSampler s) {
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = postPool_[frame_]; ai.descriptorSetCount = 1; ai.pSetLayouts = &postDsl_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(dev_, &ai, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkDescriptorImageInfo info[2] = {{s, a, la}, {s, b, lb}};
    VkWriteDescriptorSet w[2] = {};
    for (int i = 0; i < 2; i++) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = set; w[i].dstBinding = i;
        w[i].descriptorCount = 1; w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &info[i];
    }
    vkUpdateDescriptorSets(dev_, 2, w, 0, nullptr);
    return set;
}

void VkPresenter::recordTaa(VkImageView cur, VkImageLayout curLayout, uint32_t w, uint32_t h, bool fxaa) {
    Hist& dst = hist_[histCur_];
    Hist& prev = hist_[histCur_ ^ 1];
    VkDescriptorSet set = allocPostSet(cur, curLayout, histValid_ ? prev.view : cur,
                                       histValid_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : curLayout, sampLinear_);
    if (!set) return;
    VkCommandBuffer cb = cmd_[frame_];
    VkRenderPassBeginInfo rb = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = histPass_; rb.framebuffer = dst.fb; rb.renderArea.extent = {w, h};
    vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = {0, 0, (float)w, (float)h, 0, 1};
    VkRect2D sc = {{0, 0}, {w, h}};
    vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeTaa_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &set, 0, nullptr);
    PostPC pc = {{1.f / w, 1.f / h}, fxaa ? 1.f : 0.f, histValid_ ? 0.25f : 1.f};
    vkCmdPushConstants(cb, postLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRenderPass(cb);
    histCur_ ^= 1;
    histValid_ = true;
}

void VkPresenter::drawPendingGame() {
    if (!pendingPost_ || !postReady_) return;
    VkDescriptorSet set = allocPostSet(post_.view, post_.layout, post_.view, post_.layout,
                                       post_.linear || post_.fxaa ? sampLinear_ : sampNearest_);
    if (!set) return;
    VkCommandBuffer cb = cmd_[frame_];
    VkViewport vp = {(float)post_.vx, (float)post_.vy, (float)post_.vw, (float)post_.vh, 0, 1};
    VkRect2D sc = {{0, 0}, extent_};
    vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipePresent_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &set, 0, nullptr);
    PostPC pc = {{post_.rcp[0], post_.rcp[1]}, post_.fxaa ? 1.f : 0.f, 1.f};
    vkCmdPushConstants(cb, postLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
    vkCmdDraw(cb, 3, 1, 0, 0);
}

void VkPresenter::beginUiPass() {
    if (swapLayout_ == VK_IMAGE_LAYOUT_UNDEFINED) clearTarget();
    transition(images_[imageIdx_], swapLayout_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkRenderPassBeginInfo rb = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = uiPass_; rb.framebuffer = fbs_[imageIdx_]; rb.renderArea.extent = extent_;
    vkCmdBeginRenderPass(cmd_[frame_], &rb, VK_SUBPASS_CONTENTS_INLINE);
    drawPendingGame();
}

void VkPresenter::endUiPass() {
    vkCmdEndRenderPass(cmd_[frame_]);
    swapLayout_ = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    uiRendered_ = true;
}

void VkPresenter::destroyPost() {
    if (!dev_) return;
    for (auto& hh : hist_) if (hh.img) { hh.freeAt = 0; histGrave_.push_back(hh); hh = {}; }
    for (auto& h : histGrave_) {
        vkDestroyFramebuffer(dev_, h.fb, nullptr); vkDestroyImageView(dev_, h.view, nullptr);
        vkDestroyImage(dev_, h.img, nullptr); vkFreeMemory(dev_, h.mem, nullptr);
    }
    histGrave_.clear(); histW_ = histH_ = 0; histValid_ = false;
    if (pipePresent_) vkDestroyPipeline(dev_, pipePresent_, nullptr);
    if (pipeTaa_) vkDestroyPipeline(dev_, pipeTaa_, nullptr);
    if (vsMod_) vkDestroyShaderModule(dev_, vsMod_, nullptr);
    if (fsPresent_) vkDestroyShaderModule(dev_, fsPresent_, nullptr);
    if (fsTaa_) vkDestroyShaderModule(dev_, fsTaa_, nullptr);
    if (histPass_) vkDestroyRenderPass(dev_, histPass_, nullptr);
    for (auto& p : postPool_) if (p) vkDestroyDescriptorPool(dev_, p, nullptr);
    if (postLayout_) vkDestroyPipelineLayout(dev_, postLayout_, nullptr);
    if (postDsl_) vkDestroyDescriptorSetLayout(dev_, postDsl_, nullptr);
    pipePresent_ = pipeTaa_ = VK_NULL_HANDLE; vsMod_ = fsPresent_ = fsTaa_ = VK_NULL_HANDLE;
    histPass_ = VK_NULL_HANDLE; postPool_[0] = postPool_[1] = VK_NULL_HANDLE;
    postLayout_ = VK_NULL_HANDLE; postDsl_ = VK_NULL_HANDLE; postForPass_ = VK_NULL_HANDLE;
    postReady_ = false; pendingPost_ = false;
}
