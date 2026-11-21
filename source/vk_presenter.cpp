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
static unsigned s_diagLines = 0;       // registro detallado limitado (se reinicia al cargar juego)
#define VKDIAG(...) do { if (s_diagLines < 40) { s_diagLines++; LOGI(__VA_ARGS__); } } while (0)
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
    VKDIAG("[vk] set_image: image=%p view=%p layout=%d formato=%d semáforos=%u",
           (void*)img->create_info.image, (void*)img->image_view, img->image_layout, img->create_info.format, n);
}
void VkPresenter::setCommandBuffers(uint32_t n, const VkCommandBuffer* c) { coreCmds_.assign(c, c + n); }

// ------------------------------------------------------------------ init
bool VkPresenter::createInstance() {
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

    return true;
}

bool VkPresenter::createOwnDevice() {
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

    presentQueue_ = queue_;
    return true;
}

bool VkPresenter::createDeviceObjects() {
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

    fillIface();
    return createSwapchain(vsync_) && imguiInit();
}

void VkPresenter::fillIface() {
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
}

// Libera todo lo que depende del VkDevice actual (no el dispositivo en sí)
void VkPresenter::destroyDeviceObjects() {
    if (!dev_) return;
    vkDeviceWaitIdle(dev_);
    destroyUserTextures();
    destroyPost();
    imguiShutdown();
    destroySwapchain();
    for (uint32_t i = 0; i < kFrames; i++) {
        vkDestroyFence(dev_, fence_[i], nullptr);
        vkDestroySemaphore(dev_, acquired_[i], nullptr);
        vkDestroyBuffer(dev_, stage_[i], nullptr);
        vkFreeMemory(dev_, stageMem_[i], nullptr);
        fence_[i] = VK_NULL_HANDLE; acquired_[i] = VK_NULL_HANDLE; stage_[i] = VK_NULL_HANDLE; stageMem_[i] = VK_NULL_HANDLE;
    }
    if (swImg_) { vkDestroyImage(dev_, swImg_, nullptr); vkFreeMemory(dev_, swMem_, nullptr); }
    swImg_ = VK_NULL_HANDLE; swMem_ = VK_NULL_HANDLE; swFmt_ = VK_FORMAT_UNDEFINED; swLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    if (uiPass_) vkDestroyRenderPass(dev_, uiPass_, nullptr);
    uiPass_ = VK_NULL_HANDLE;
    vkDestroyCommandPool(dev_, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    hasCoreImg_ = false; pendingGame_ = false; frame_ = 0; inFrame_ = false;
}

void VkPresenter::destroyDevice() {
    if (!dev_) return;
    // Si el dispositivo lo creó el núcleo, primero le dejamos liberar lo suyo
    if (coreNeg_ && coreNeg_->destroy_device) coreNeg_->destroy_device();
    coreNeg_ = nullptr;
    vkDestroyDevice(dev_, nullptr);
    dev_ = VK_NULL_HANDLE;
}

// create_device2: el núcleo arma su VkDeviceCreateInfo y nosotros lo creamos añadiendo VK_KHR_swapchain
static VkDevice RETRO_CALLCONV wrapCreateDevice(VkPhysicalDevice gpu, void*, const VkDeviceCreateInfo* ci) {
    VkDeviceCreateInfo info = *ci;
    std::vector<const char*> exts(ci->ppEnabledExtensionNames, ci->ppEnabledExtensionNames + ci->enabledExtensionCount);
    bool has = false;
    for (auto* e : exts) if (!strcmp(e, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) has = true;
    if (!has) exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    info.enabledExtensionCount = (uint32_t)exts.size();
    info.ppEnabledExtensionNames = exts.data();
    for (auto* e : exts) LOGI("[vk]   extensión de dispositivo: %s", e);
    VkDevice dev = VK_NULL_HANDLE;
    VkResult r = vkCreateDevice(gpu, &info, nullptr, &dev);
    LOGI("[vk] vkCreateDevice (pedido por el núcleo) = %d", r);
    return r == VK_SUCCESS ? dev : VK_NULL_HANDLE;
}

void VkPresenter::releaseCoreDevice() {
    if (!coreNeg_) return;
    LOGI("[vk] liberando el dispositivo del núcleo");
    destroyDeviceObjects();
    destroyDevice();
    createOwnDevice();
    createDeviceObjects();
}

bool VkPresenter::adoptCoreDevice(const retro_hw_render_context_negotiation_interface_vulkan* neg) {
    LOGI("[vk] negociación v%u: el núcleo crea el dispositivo", neg->interface_version);
    destroyDeviceObjects();
    destroyDevice();
    retro_vulkan_context ctx = {};
    bool ok = false;
    if (neg->interface_version >= 2 && neg->create_device2)
        ok = neg->create_device2(&ctx, inst_, gpu_, surf_, vkGetInstanceProcAddr, wrapCreateDevice, this);
    else if (neg->create_device) {
        static const char* ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        static const VkPhysicalDeviceFeatures noFeatures = {};
        ok = neg->create_device(&ctx, inst_, gpu_, surf_, vkGetInstanceProcAddr, ext, 1, nullptr, 0, &noFeatures);
    }
    if (!ok || !ctx.device) {
        LOGI("[vk] el núcleo no pudo crear su dispositivo; se vuelve al propio (el juego no se verá)");
        createOwnDevice();
        createDeviceObjects();
        return false;
    }
    coreNeg_ = neg;
    dev_ = ctx.device; gpu_ = ctx.gpu ? ctx.gpu : gpu_;
    queue_ = ctx.queue; qfam_ = ctx.queue_family_index;
    presentQueue_ = ctx.presentation_queue ? ctx.presentation_queue : ctx.queue;
    LOGI("[vk] dispositivo del núcleo listo: cola familia %u, presentación familia %u",
         ctx.queue_family_index, ctx.presentation_queue_family_index);
    s_diagLines = 0;
    return createDeviceObjects();
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
    appliedMode_ = -1;   // se vuelven a aplicar las tasas de la pantalla 120 Hz en el siguiente frame
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
    vsync_ = vsync;
    // ImGui se inicializa aparte (Menu::init) la primera vez
    if (!createInstance() || !createOwnDevice()) return false;
    imguiDeferred_ = true;
    bool ok = createDeviceObjects();
    imguiDeferred_ = false;
    return ok;
}

bool VkPresenter::resize(int w, int h, bool vsync) {
    vsync_ = vsync;
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
    if (!inst_) return;
    destroyDeviceObjects();
    destroyDevice();
    vkDestroySurfaceKHR(inst_, surf_, nullptr);
    vkDestroyInstance(inst_, nullptr);
    inst_ = VK_NULL_HANDLE;
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
    if (!acquiredOk_) { static unsigned n = 0; if (n++ < 10) LOGI("[vk] vkAcquireNextImageKHR = %d", r); }
    vkResetFences(dev_, 1, &fence_[frame_]);
    vkResetCommandBuffer(cmd_[frame_], 0);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_[frame_], &bi);
    swapLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    coreCmds_.clear(); coreWait_.clear(); coreSignal_ = VK_NULL_HANDLE;
    pendingGame_ = false; pendingPost_ = false; uiRendered_ = false;
    if (postPool_[frame_]) vkResetDescriptorPool(dev_, postPool_[frame_], 0);   // la fence de este índice ya se esperó
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
    VKDIAG("[vk] presentCore %ux%u -> viewport %d,%d %dx%d (imagen del núcleo: %s)", w, h, vx, vy, vw, vh,
           hasCoreImg_ ? "sí" : "NO");
    clearTarget();
    if (!hasCoreImg_ || !uiReady_) return;
    // Camino principal: pipeline propio (FXAA / TAA). El de ImGui queda solo como respaldo.
    if (ensurePost()) {
        VkImageView src = coreImg_.image_view;
        VkImageLayout lay = coreImg_.image_layout;
        bool fx = aaFxaa_;
        float fxW = (float)w / fxDiv_, fxH = (float)h / fxDiv_;   // píxeles originales de la PS1 (efectos)
        // ScaleFX y escaladores de píxel: solo con imagen nativa o casi (1x-2x); a 4x u 8x no aportan
        bool small = w <= 1024 && h <= 576;
        if (sfxOn_ && small && ensureSfx(w, h)) {
            recordSfx(src, lay, w, h);
            src = sfx_[4].view; lay = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            w *= 3; h *= 3;
        } else if (pixMode_ && small) {
            uint32_t f = pixMode_ == 3 ? 3 : 2;
            if (ensurePix(w, h, f)) {
                recordPass(pipePix_, histPass_, pix_, w * f, h * f, src, lay, src, lay, 1.f / w, 1.f / h, (float)pixMode_);
                src = pix_.view; lay = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                w *= f; h *= f;
            }
        }
        if (aaTaa_ && ensureHistory(w, h)) {
            recordTaa(src, lay, w, h, fx);
            src = hist_[histCur_ ^ 1].view;            // la que se acaba de escribir
            lay = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            fx = false;
        }
        post_ = {src, lay, linear, fx, {1.f / w, 1.f / h}, vx, vy, vw, vh};
        post_.effect = (float)fxEffect_; post_.fxSize[0] = fxW; post_.fxSize[1] = fxH;
        pendingPost_ = true;
        // Frames intermedios (120 Hz): se guarda una copia propia del frame y esta presentación muestra
        // la mezcla con el anterior; el frame real va en la presentación extra (Video::extraPresent).
        if (interpLive_ && ensureFg(w, h)) {
            int cur = fgValid_ ? fgLatest_ ^ 1 : 0;
            recordCopy(src, lay, fg_[cur], w, h);
            fgPost_ = post_;
            fgPost_.view = fg_[cur].view; fgPost_.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            fgPost_.blendWith = VK_NULL_HANDLE;
            post_ = fgPost_;
            if (fgValid_) { post_.view = fg_[fgLatest_].view; post_.blendWith = fg_[cur].view; }
            fgLatest_ = cur;
            fgValid_ = true;
        }
        return;
    }
    // Descriptor (sampler + vista) cacheado por vista de imagen
    VkImageView view = coreImg_.image_view;
    VkImageLayout layout = coreImg_.image_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    for (auto& e : texCache_)
        if (e.view == view && e.linear == linear && e.layout == layout) { set = e.set; e.lastUse = frameCount_; }
    if (!set) {
        // Vista propia con alfa = 1: ImGui mezcla por alfa y el núcleo puede dejarlo en 0
        VkImageViewCreateInfo vi = coreImg_.create_info;
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vi.pNext = nullptr;
        vi.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_ONE};
        VkImageView opaque = VK_NULL_HANDLE;
        if (vkCreateImageView(dev_, &vi, nullptr, &opaque) != VK_SUCCESS) opaque = view;
        set = ImGui_ImplVulkan_AddTexture(linear ? sampLinear_ : sampNearest_, opaque, layout);
        texCache_.push_back({view, linear, layout, set, opaque == view ? VK_NULL_HANDLE : opaque, frameCount_});
        static unsigned logged = 0;
        if (logged++ < 12) LOGI("[vk] textura del núcleo registrada (vista %p, layout %d, en caché %zu)", (void*)view, layout, texCache_.size());
    }
    pendingGame_ = true; pendingSet_ = set;
    pendingRect_[0] = (float)vx; pendingRect_[1] = (float)vy;
    pendingRect_[2] = (float)(vx + vw); pendingRect_[3] = (float)(vy + vh);
}

uint64_t VkPresenter::createTexture(const uint8_t* rgba, int w, int h) {
    if (!uiReady_ || !dev_ || w <= 0 || h <= 0) return 0;
    VkDeviceSize size = (VkDeviceSize)w * h * 4;
    UserTex t = {};
    VkImageCreateInfo ii = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_UNORM; ii.extent = {(uint32_t)w, (uint32_t)h, 1};
    ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (vkCreateImage(dev_, &ii, nullptr, &t.img) != VK_SUCCESS) return 0;
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev_, t.img, &mr);
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size; mai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(dev_, &mai, nullptr, &t.mem);
    vkBindImageMemory(dev_, t.img, t.mem, 0);

    // Buffer temporal de subida
    VkBuffer sb; VkDeviceMemory sm;
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(dev_, &bci, nullptr, &sb);
    VkMemoryRequirements br; vkGetBufferMemoryRequirements(dev_, sb, &br);
    mai.allocationSize = br.size;
    mai.memoryTypeIndex = memType(br.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(dev_, &mai, nullptr, &sm);
    vkBindBufferMemory(dev_, sb, sm, 0);
    void* map; vkMapMemory(dev_, sm, 0, size, 0, &map); memcpy(map, rgba, size); vkUnmapMemory(dev_, sm);

    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(dev_, &cai, &cb);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.img; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy c = {};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyBufferToImage(cb, sb, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(cb);
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence f; vkCreateFence(dev_, &fci, nullptr, &f);
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    { std::lock_guard<std::mutex> lk(queueLock); vkQueueSubmit(queue_, 1, &si, f); }
    vkWaitForFences(dev_, 1, &f, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev_, f, nullptr);
    vkFreeCommandBuffers(dev_, pool_, 1, &cb);
    vkDestroyBuffer(dev_, sb, nullptr); vkFreeMemory(dev_, sm, nullptr);

    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(dev_, &vi, nullptr, &t.view);
    t.set = ImGui_ImplVulkan_AddTexture(sampLinear_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    userTex_.push_back(t);
    return (uint64_t)t.set;
}

void VkPresenter::destroyTexture(uint64_t id) {
    for (size_t i = 0; i < userTex_.size(); i++)
        if ((uint64_t)userTex_[i].set == id) {
            userTex_[i].freeAt = frameCount_ + kFrames + 2;   // cuando ningún frame en vuelo la use
            userTexGrave_.push_back(userTex_[i]);
            userTex_.erase(userTex_.begin() + i);
            return;
        }
}

void VkPresenter::destroyUserTextures() {
    auto kill = [&](UserTex& t) {
        if (uiReady_) ImGui_ImplVulkan_RemoveTexture(t.set);
        vkDestroyImageView(dev_, t.view, nullptr); vkDestroyImage(dev_, t.img, nullptr); vkFreeMemory(dev_, t.mem, nullptr);
    };
    for (auto& t : userTex_) kill(t);
    for (auto& t : userTexGrave_) kill(t);
    userTex_.clear(); userTexGrave_.clear();
    gen_++;
}

void VkPresenter::collectTextures() {
    for (size_t i = 0; i < userTexGrave_.size();) {
        if (frameCount_ >= userTexGrave_[i].freeAt) {
            UserTex& t = userTexGrave_[i];
            ImGui_ImplVulkan_RemoveTexture(t.set);
            vkDestroyImageView(dev_, t.view, nullptr); vkDestroyImage(dev_, t.img, nullptr); vkFreeMemory(dev_, t.mem, nullptr);
            userTexGrave_.erase(userTexGrave_.begin() + i);
        } else i++;
    }
    // Entradas sin usar desde hace 3 s pasan al "cementerio"; se destruyen cuando ningún
    // frame en vuelo puede estar usándolas (sin vkDeviceWaitIdle: el núcleo usa la GPU en paralelo)
    for (size_t i = 0; i < texCache_.size();) {
        if (frameCount_ - texCache_[i].lastUse > 180) {
            texCache_[i].lastUse = frameCount_;
            texGraveyard_.push_back(texCache_[i]);
            texCache_.erase(texCache_.begin() + i);
        } else i++;
    }
    for (size_t i = 0; i < texGraveyard_.size();) {
        if (frameCount_ - texGraveyard_[i].lastUse > kFrames + 2) {
            ImGui_ImplVulkan_RemoveTexture(texGraveyard_[i].set);
            if (texGraveyard_[i].opaqueView) vkDestroyImageView(dev_, texGraveyard_[i].opaqueView, nullptr);
            texGraveyard_.erase(texGraveyard_.begin() + i);
        } else i++;
    }
}

void VkPresenter::addGameLayer() {
    if (!pendingGame_ || pendingPost_) return;   // con posprocesado el juego lo dibuja su pipeline
    // Se dibuja al fondo; el menú y el OSD quedan encima
    ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)pendingSet_,
        ImVec2(pendingRect_[0], pendingRect_[1]), ImVec2(pendingRect_[2], pendingRect_[3]));
}

void VkPresenter::presentSw(const void* data, unsigned w, unsigned h, size_t pitch, bool xrgb,
                            int vx, int vy, int vw, int vh, bool linear) {
    VKDIAG("[vk] presentSw %ux%u pitch=%zu %s", w, h, pitch, xrgb ? "XRGB8888" : "RGB565");
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
    if (pendingPost_ && !uiRendered_ && acquiredOk_) { beginUiPass(); endUiPass(); }   // frame sin interfaz
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
        VkResult sr = vkQueueSubmit(queue_, 1, &si, fence_[frame_]);
        VKDIAG("[vk] frame: cmds núcleo=%zu esperas=%zu señales=%zu layout final=%d submit=%d",
               coreCmds_.size(), wait.size(), signal.size(), swapLayout_, sr);
        if (sr != VK_SUCCESS) { static unsigned n = 0; if (n++ < 10) LOGI("[vk] vkQueueSubmit = %d", sr); }
        if (acquiredOk_) {
            VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderDone_[imageIdx_];
            pi.swapchainCount = 1; pi.pSwapchains = &swap_; pi.pImageIndices = &imageIdx_;
            VkResult pr = vkQueuePresentKHR(presentQueue_, &pi);
            if (pr != VK_SUCCESS) { static unsigned n = 0; if (n++ < 10) LOGI("[vk] vkQueuePresentKHR = %d", pr); }
        }
    }
    frame_ = (frame_ + 1) % kFrames;
    frameCount_++;
    if (uiReady_) collectTextures();
    collectHistory();
    inFrame_ = false;
}

// ------------------------------------------------------------------ ImGui
bool VkPresenter::imguiInit() {
    if (imguiDeferred_ || uiReady_) return true;
    for (int i = 0; i < 2; i++) {
        VkSamplerCreateInfo sci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter = sci.minFilter = i ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod = 0.f;
        VKCHECK(vkCreateSampler(dev_, &sci, nullptr, i ? &sampLinear_ : &sampNearest_));
    }
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; dpi.maxSets = 64;
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
    for (auto& e : texCache_) { ImGui_ImplVulkan_RemoveTexture(e.set); if (e.opaqueView) vkDestroyImageView(dev_, e.opaqueView, nullptr); }
    for (auto& e : texGraveyard_) { ImGui_ImplVulkan_RemoveTexture(e.set); if (e.opaqueView) vkDestroyImageView(dev_, e.opaqueView, nullptr); }
    texCache_.clear(); texGraveyard_.clear();
    ImGui_ImplVulkan_Shutdown();
    vkDestroyDescriptorPool(dev_, uiPool_, nullptr);
    vkDestroySampler(dev_, sampNearest_, nullptr);
    vkDestroySampler(dev_, sampLinear_, nullptr);
    uiReady_ = false;
}

void VkPresenter::imguiNewFrame() { ImGui_ImplVulkan_NewFrame(); }

void VkPresenter::imguiRender(ImDrawData* dd) {
    if (!acquiredOk_ || !inFrame_) return;
    VKDIAG("[vk] imguiRender: listas=%d vértices=%d juego=%s", dd->CmdListsCount, dd->TotalVtxCount, pendingGame_ ? "sí" : "no");
    if (uiRendered_) return;
    beginUiPass();                       // dibuja también el juego (posprocesado) antes de la interfaz
    ImGui_ImplVulkan_RenderDrawData(dd, cmd_[frame_]);
    endUiPass();
}
