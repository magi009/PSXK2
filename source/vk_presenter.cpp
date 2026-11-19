#include "vk_presenter.h"
#include "log.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <switch.h>
#include <algorithm>
#include <cstring>

#define VKCHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { LOGI("[vk] %s = %d", #x, r_); return false; } } while (0)

// ------------------------------------------------------------------ callbacks libretro -> presenter
static VkPresenter* P(void* h) { return (VkPresenter*)h; }
static void RETRO_CALLCONV cb_set_image(void* h, const retro_vulkan_image* img, uint32_t n, const VkSemaphore* s, uint32_t) {
    P(h)->setImage(img, n, s);
}
static uint32_t RETRO_CALLCONV cb_sync_index(void* h) { return P(h)->syncIndex(); }
static uint32_t RETRO_CALLCONV cb_sync_mask(void* h) { return P(h)->syncMask(); }
static void RETRO_CALLCONV cb_set_cmds(void* h, uint32_t n, const VkCommandBuffer* c) { P(h)->setCommandBuffers(n, c); }
static void RETRO_CALLCONV cb_wait_sync(void*) {}      // beginFrame() ya esperó la fence de este índice
static void RETRO_CALLCONV cb_lock(void* h) { P(h)->queueLock.lock(); }
static void RETRO_CALLCONV cb_unlock(void* h) { P(h)->queueLock.unlock(); }
static void RETRO_CALLCONV cb_signal(void* h, VkSemaphore s) { P(h)->setSignalSemaphore(s); }

void VkPresenter::setImage(const retro_vulkan_image* img, uint32_t n, const VkSemaphore* s) {
    coreImg_ = *img; hasCoreImg_ = true;
    coreWait_.assign(s, s + n);
}
void VkPresenter::setCommandBuffers(uint32_t n, const VkCommandBuffer* c) { coreCmds_.assign(c, c + n); }

// ------------------------------------------------------------------ init
bool VkPresenter::createDevice() {
    // Instancia + superficie VI (VK_NN_vi_surface sobre la NWindow de libounce)
    const char* iext[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_NN_VI_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "PSX-K2"; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app; ici.enabledExtensionCount = 2; ici.ppEnabledExtensionNames = iext;
    VKCHECK(vkCreateInstance(&ici, nullptr, &inst_));

    VkViSurfaceCreateInfoNN sci = {VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN};
    sci.window = nwindowGetDefault();
    VKCHECK(vkCreateViSurfaceNN(inst_, &sci, nullptr, &surf_));

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst_, &n, nullptr);
    if (!n) { LOGI("[vk] sin GPU"); return false; }
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(inst_, &n, gpus.data());
    gpu_ = gpus[0];
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(gpu_, &props);
    LOGI("[vk] GPU %s, Vulkan %u.%u.%u", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
         VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));

    // Cola gráfica que además pueda presentar
    vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qf(n);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &n, qf.data());
    bool found = false;
    for (uint32_t i = 0; i < n && !found; i++) {
        VkBool32 pres = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu_, i, surf_, &pres);
        if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) { qfam_ = i; found = true; }
    }
    if (!found) { LOGI("[vk] sin cola gráfica con presentación"); return false; }

    // Se activan TODAS las funciones soportadas: el renderer de Beetle (parallel-psx)
    // necesita varias (storage images, sample rate shading, 16/8-bit storage...).
    VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features f11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    f11.pNext = &f12;
    VkPhysicalDeviceFeatures2 feats = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    feats.pNext = &f11;
    vkGetPhysicalDeviceFeatures2(gpu_, &feats);
    feats.features.robustBufferAccess = VK_FALSE;   // coste de rendimiento, no hace falta

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfam_; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    const char* dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &feats;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = dext;
    VKCHECK(vkCreateDevice(gpu_, &dci, nullptr, &dev_));
    vkGetDeviceQueue(dev_, qfam_, 0, &queue_);

    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = qfam_;
    VKCHECK(vkCreateCommandPool(dev_, &pci, nullptr, &pool_));
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = kFrames;
    VKCHECK(vkAllocateCommandBuffers(dev_, &cai, cmd_));
    for (uint32_t i = 0; i < kFrames; i++) {
        VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKCHECK(vkCreateFence(dev_, &fci, nullptr, &fence_[i]));
        VkSemaphoreCreateInfo s = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VKCHECK(vkCreateSemaphore(dev_, &s, nullptr, &acquired_[i]));
        // buffer de subida para el renderer por software
        VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = kStageSize; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VKCHECK(vkCreateBuffer(dev_, &bci, nullptr, &stage_[i]));
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev_, stage_[i], &mr);
        VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VKCHECK(vkAllocateMemory(dev_, &mai, nullptr, &stageMem_[i]));
        vkBindBufferMemory(dev_, stage_[i], stageMem_[i], 0);
        vkMapMemory(dev_, stageMem_[i], 0, kStageSize, 0, &stageMap_[i]);
    }

    iface_.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    iface_.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    iface_.handle = this;
    iface_.instance = inst_; iface_.gpu = gpu_; iface_.device = dev_;
    iface_.get_device_proc_addr = vkGetDeviceProcAddr;
    iface_.get_instance_proc_addr = vkGetInstanceProcAddr;
    iface_.queue = queue_; iface_.queue_index = qfam_;
    iface_.set_image = cb_set_image;
    iface_.get_sync_index = cb_sync_index;
    iface_.get_sync_index_mask = cb_sync_mask;
    iface_.set_command_buffers = cb_set_cmds;
    iface_.wait_sync_index = cb_wait_sync;
    iface_.lock_queue = cb_lock;
    iface_.unlock_queue = cb_unlock;
    iface_.set_signal_semaphore = cb_signal;
    return true;
}

uint32_t VkPresenter::memType(uint32_t bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(gpu_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return 0;
}

bool VkPresenter::createSwapchain(bool vsync) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surf_, &caps);
    if (caps.currentExtent.width != 0xFFFFFFFF) extent_ = caps.currentExtent;

    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surf_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surf_, &n, fmts.data());
    VkFormat old = format_;
    format_ = fmts.empty() ? VK_FORMAT_B8G8R8A8_UNORM : fmts[0].format;
    for (auto& f : fmts) if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) { format_ = f.format; break; }

    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surf_, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surf_, &n, modes.data());
        for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR || m == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = m;
    }

    VkSwapchainCreateInfoKHR ci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surf_;
    ci.minImageCount = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount) ci.minImageCount = caps.maxImageCount;
    ci.imageFormat = format_; ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = extent_; ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = mode; ci.clipped = VK_TRUE;
    VKCHECK(vkCreateSwapchainKHR(dev_, &ci, nullptr, &swap_));

    vkGetSwapchainImagesKHR(dev_, swap_, &n, nullptr);
    images_.resize(n);
    vkGetSwapchainImagesKHR(dev_, swap_, &n, images_.data());

    if (!uiPass_ || old != format_) {
        if (uiPass_) vkDestroyRenderPass(dev_, uiPass_, nullptr);
        VkAttachmentDescription att = {};
        att.format = format_; att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub = {}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        VkRenderPassCreateInfo rpi = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpi.attachmentCount = 1; rpi.pAttachments = &att; rpi.subpassCount = 1; rpi.pSubpasses = &sub;
        VKCHECK(vkCreateRenderPass(dev_, &rpi, nullptr, &uiPass_));
    }
    for (auto img : images_) {
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v; VKCHECK(vkCreateImageView(dev_, &vi, nullptr, &v)); views_.push_back(v);
        VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = uiPass_; fi.attachmentCount = 1; fi.pAttachments = &v;
        fi.width = extent_.width; fi.height = extent_.height; fi.layers = 1;
        VkFramebuffer fb; VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &fb)); fbs_.push_back(fb);
        VkSemaphoreCreateInfo s = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore sem; VKCHECK(vkCreateSemaphore(dev_, &s, nullptr, &sem)); renderDone_.push_back(sem);
    }
    LOGI("[vk] swapchain %ux%u, %zu imágenes, modo %d", extent_.width, extent_.height, images_.size(), mode);
    return true;
}

void VkPresenter::destroySwapchain() {
    vkDeviceWaitIdle(dev_);
    for (auto f : fbs_) vkDestroyFramebuffer(dev_, f, nullptr);
    for (auto v : views_) vkDestroyImageView(dev_, v, nullptr);
    for (auto s : renderDone_) vkDestroySemaphore(dev_, s, nullptr);
    fbs_.clear(); views_.clear(); renderDone_.clear(); images_.clear();
    if (swap_) vkDestroySwapchainKHR(dev_, swap_, nullptr);
    swap_ = VK_NULL_HANDLE;
}

bool VkPresenter::init(int w, int h, bool vsync) {
    nwindowSetDimensions(nwindowGetDefault(), w, h);
    extent_ = {(uint32_t)w, (uint32_t)h};
    return createDevice() && createSwapchain(vsync);
}

bool VkPresenter::resize(int w, int h, bool vsync) {
    destroySwapchain();
    if (R_FAILED(nwindowSetDimensions(nwindowGetDefault(), w, h))) {
        LOGI("[vk] el sistema rechazó %dx%d, usando 1920x1080", w, h);
        w = 1920; h = 1080;
        nwindowSetDimensions(nwindowGetDefault(), w, h);
    }
    extent_ = {(uint32_t)w, (uint32_t)h};
    return createSwapchain(vsync);
}

void VkPresenter::shutdown() {
    if (!dev_) return;
    vkDeviceWaitIdle(dev_);
    imguiShutdown();
    destroySwapchain();
    for (uint32_t i = 0; i < kFrames; i++) {
        vkDestroyFence(dev_, fence_[i], nullptr);
        vkDestroySemaphore(dev_, acquired_[i], nullptr);
        vkDestroyBuffer(dev_, stage_[i], nullptr);
        vkFreeMemory(dev_, stageMem_[i], nullptr);
    }
    if (swImg_) { vkDestroyImage(dev_, swImg_, nullptr); vkFreeMemory(dev_, swMem_, nullptr); }
    if (uiPass_) vkDestroyRenderPass(dev_, uiPass_, nullptr);
    vkDestroyCommandPool(dev_, pool_, nullptr);
    vkDestroyDevice(dev_, nullptr);
    vkDestroySurfaceKHR(inst_, surf_, nullptr);
    vkDestroyInstance(inst_, nullptr);
    dev_ = VK_NULL_HANDLE;
}

// ------------------------------------------------------------------ frame
void VkPresenter::transition(VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags srcA,
                             VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from; b.newLayout = to; b.srcAccessMask = srcA; b.dstAccessMask = dstA;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd_[frame_], srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void VkPresenter::beginFrame() {
    if (inFrame_) return;
    vkWaitForFences(dev_, 1, &fence_[frame_], VK_TRUE, UINT64_MAX);
    VkResult r = vkAcquireNextImageKHR(dev_, swap_, UINT64_MAX, acquired_[frame_], VK_NULL_HANDLE, &imageIdx_);
    acquiredOk_ = (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR);
    vkResetFences(dev_, 1, &fence_[frame_]);
    vkResetCommandBuffer(cmd_[frame_], 0);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_[frame_], &bi);
    swapLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    coreCmds_.clear(); coreWait_.clear(); coreSignal_ = VK_NULL_HANDLE;
    inFrame_ = true;
}

void VkPresenter::clearTarget() {
    if (!acquiredOk_) return;
    VkImage img = images_[imageIdx_];
    transition(img, swapLayout_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkClearColorValue black = {{0.f, 0.f, 0.f, 1.f}};
    VkImageSubresourceRange rng = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd_[frame_], img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &rng);
    swapLayout_ = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

void VkPresenter::blit(VkImage src, VkImageLayout srcLayout, unsigned w, unsigned h,
                       int vx, int vy, int vw, int vh, bool linear) {
    clearTarget();
    if (!acquiredOk_) return;
    if (srcLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        transition(src, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageBlit r = {};
    r.srcSubresource = r.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.srcOffsets[1] = {(int32_t)w, (int32_t)h, 1};
    r.dstOffsets[0] = {vx, vy, 0};
    r.dstOffsets[1] = {vx + vw, vy + vh, 1};
    vkCmdBlitImage(cmd_[frame_], src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, images_[imageIdx_],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    if (srcLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && srcLayout != VK_IMAGE_LAYOUT_UNDEFINED)
        transition(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcLayout, VK_ACCESS_TRANSFER_READ_BIT,
                   VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
}

void VkPresenter::presentCore(unsigned w, unsigned h, int vx, int vy, int vw, int vh, bool linear) {
    if (!hasCoreImg_) { clearTarget(); return; }
    blit(coreImg_.create_info.image, coreImg_.image_layout, w, h, vx, vy, vw, vh, linear);
}

void VkPresenter::presentSw(const void* data, unsigned w, unsigned h, size_t pitch, bool xrgb,
                            int vx, int vy, int vw, int vh, bool linear) {
    VkFormat fmt = xrgb ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16;
    unsigned bpp = xrgb ? 4 : 2;
    if ((VkDeviceSize)pitch * h > kStageSize) return;
    if (!swImg_ || swFmt_ != fmt) {
        vkDeviceWaitIdle(dev_);
        if (swImg_) { vkDestroyImage(dev_, swImg_, nullptr); vkFreeMemory(dev_, swMem_, nullptr); }
        VkImageCreateInfo ii = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt; ii.extent = {1024, 512, 1};
        ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        vkCreateImage(dev_, &ii, nullptr, &swImg_);
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev_, swImg_, &mr);
        VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = mr.size; mai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(dev_, &mai, nullptr, &swMem_);
        vkBindImageMemory(dev_, swImg_, swMem_, 0);
        swFmt_ = fmt; swLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    if (data) {
        memcpy(stageMap_[frame_], data, pitch * h);
        transition(swImg_, swLayout_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy c = {};
        c.bufferRowLength = (uint32_t)(pitch / bpp);
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(cmd_[frame_], stage_[frame_], swImg_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        transition(swImg_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        swLayout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    if (swLayout_ == VK_IMAGE_LAYOUT_UNDEFINED) { clearTarget(); return; }
    blit(swImg_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, w, h, vx, vy, vw, vh, linear);
}

void VkPresenter::endFrame() {
    if (!inFrame_) return;
    if (swapLayout_ == VK_IMAGE_LAYOUT_UNDEFINED) clearTarget();
    if (acquiredOk_ && swapLayout_ != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        transition(images_[imageIdx_], swapLayout_, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    vkEndCommandBuffer(cmd_[frame_]);

    std::vector<VkCommandBuffer> cmds = coreCmds_;      // primero los del núcleo
    cmds.push_back(cmd_[frame_]);
    std::vector<VkSemaphore> wait = coreWait_;
    std::vector<VkPipelineStageFlags> stages(wait.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    if (acquiredOk_) { wait.push_back(acquired_[frame_]); stages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT); }
    std::vector<VkSemaphore> signal;
    if (acquiredOk_) signal.push_back(renderDone_[imageIdx_]);
    if (coreSignal_) signal.push_back(coreSignal_);

    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = (uint32_t)wait.size(); si.pWaitSemaphores = wait.data(); si.pWaitDstStageMask = stages.data();
    si.commandBufferCount = (uint32_t)cmds.size(); si.pCommandBuffers = cmds.data();
    si.signalSemaphoreCount = (uint32_t)signal.size(); si.pSignalSemaphores = signal.data();
    {
        std::lock_guard<std::mutex> lk(queueLock);
        vkQueueSubmit(queue_, 1, &si, fence_[frame_]);
        if (acquiredOk_) {
            VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderDone_[imageIdx_];
            pi.swapchainCount = 1; pi.pSwapchains = &swap_; pi.pImageIndices = &imageIdx_;
            vkQueuePresentKHR(queue_, &pi);
        }
    }
    frame_ = (frame_ + 1) % kFrames;
    inFrame_ = false;
}

// ------------------------------------------------------------------ ImGui
bool VkPresenter::imguiInit() {
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; dpi.maxSets = 16;
    dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps;
    VKCHECK(vkCreateDescriptorPool(dev_, &dpi, nullptr, &uiPool_));
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.Instance = inst_; ii.PhysicalDevice = gpu_; ii.Device = dev_;
    ii.QueueFamily = qfam_; ii.Queue = queue_; ii.DescriptorPool = uiPool_;
    ii.RenderPass = uiPass_; ii.MinImageCount = 2; ii.ImageCount = (uint32_t)images_.size();
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    uiReady_ = ImGui_ImplVulkan_Init(&ii);
    return uiReady_;
}

void VkPresenter::imguiShutdown() {
    if (!uiReady_) return;
    vkDeviceWaitIdle(dev_);
    ImGui_ImplVulkan_Shutdown();
    vkDestroyDescriptorPool(dev_, uiPool_, nullptr);
    uiReady_ = false;
}

void VkPresenter::imguiNewFrame() { ImGui_ImplVulkan_NewFrame(); }

void VkPresenter::imguiRender(ImDrawData* dd) {
    if (!acquiredOk_ || !inFrame_) return;
    if (swapLayout_ == VK_IMAGE_LAYOUT_UNDEFINED) clearTarget();
    transition(images_[imageIdx_], swapLayout_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkRenderPassBeginInfo rb = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = uiPass_; rb.framebuffer = fbs_[imageIdx_]; rb.renderArea.extent = extent_;
    vkCmdBeginRenderPass(cmd_[frame_], &rb, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(dd, cmd_[frame_]);
    vkCmdEndRenderPass(cmd_[frame_]);
    swapLayout_ = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}
