#pragma once
// Backend Vulkan (OunceVK, Vulkan 1.4 en Switch 2).
// - Crea instancia, superficie VI, dispositivo y swapchain.
// - Expone al núcleo retro_hw_render_interface_vulkan (Beetle PSX HW renderer "hardware_vk").
// - Escala la imagen del núcleo con vkCmdBlitImage y dibuja ImGui encima.
#define VK_USE_PLATFORM_VI_NN
#include <vulkan/vulkan.h>
#include <libretro.h>
#include <libretro_vulkan.h>
#include <mutex>
#include <vector>

struct ImDrawData;

class VkPresenter {
public:
    bool init(int w, int h, bool vsync);
    void shutdown();
    bool resize(int w, int h, bool vsync);

    void beginFrame();
    void endFrame();

    // Imagen del núcleo (tras set_image) o frame por software
    void presentCore(unsigned w, unsigned h, int vx, int vy, int vw, int vh, bool linear);
    void presentSw(const void* data, unsigned w, unsigned h, size_t pitch, bool xrgb8888,
                   int vx, int vy, int vw, int vh, bool linear);

    // Dibuja la imagen del núcleo como textura (vía ImGui) en el frame actual
    void addGameLayer();

    bool imguiInit();
    void imguiShutdown();
    void imguiNewFrame();
    void imguiRender(ImDrawData* dd);

    void resetCore() { hasCoreImg_ = false; }
    // Negociación libretro: el núcleo crea el VkDevice (Beetle lo exige) y lo adoptamos
    bool adoptCoreDevice(const retro_hw_render_context_negotiation_interface_vulkan* neg);
    void releaseCoreDevice();   // tras context_destroy: vuelve al dispositivo propio
    retro_hw_render_interface_vulkan* iface() { return &iface_; }
    int width() const { return extent_.width; }
    int height() const { return extent_.height; }

    // Callbacks del núcleo
    void setImage(const retro_vulkan_image* img, uint32_t nsem, const VkSemaphore* sems);
    void setCommandBuffers(uint32_t n, const VkCommandBuffer* cmds);
    void setSignalSemaphore(VkSemaphore s) { coreSignal_ = s; }
    uint32_t syncIndex() const { return frame_; }
    uint32_t syncMask() const { return (1u << kFrames) - 1; }
    std::mutex queueLock;

private:
    bool createInstance();
    bool createOwnDevice();
    bool createDeviceObjects();
    void destroyDeviceObjects();
    void destroyDevice();
    void fillIface();
    bool createSwapchain(bool vsync);
    void destroySwapchain();
    void transition(VkImage img, VkImageLayout from, VkImageLayout to,
                    VkAccessFlags srcA, VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS);
    void clearTarget();
    void blit(VkImage src, VkImageLayout srcLayout, unsigned w, unsigned h,
              int vx, int vy, int vw, int vh, bool linear);
    uint32_t memType(uint32_t bits, VkMemoryPropertyFlags props);

    static constexpr uint32_t kFrames = 2;
    VkInstance inst_ = VK_NULL_HANDLE;
    VkSurfaceKHR surf_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkDevice dev_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    const retro_hw_render_context_negotiation_interface_vulkan* coreNeg_ = nullptr;
    bool vsync_ = true;
    bool imguiDeferred_ = false;
    uint32_t qfam_ = 0;
    VkSwapchainKHR swap_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent_ = {1920, 1080};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> fbs_;
    std::vector<VkSemaphore> renderDone_;   // uno por imagen del swapchain
    VkRenderPass uiPass_ = VK_NULL_HANDLE;
    VkDescriptorPool uiPool_ = VK_NULL_HANDLE;
    bool uiReady_ = false;

    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_[kFrames] = {};
    VkFence fence_[kFrames] = {};
    VkSemaphore acquired_[kFrames] = {};
    uint32_t frame_ = 0, imageIdx_ = 0;
    bool inFrame_ = false, acquiredOk_ = false;
    VkImageLayout swapLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // Estado del núcleo para este frame
    retro_vulkan_image coreImg_ = {};
    bool hasCoreImg_ = false;
    std::vector<VkSemaphore> coreWait_;
    std::vector<VkCommandBuffer> coreCmds_;
    VkSemaphore coreSignal_ = VK_NULL_HANDLE;

    // Imagen del núcleo como textura (muestreo, no blit: el núcleo solo garantiza uso SAMPLED)
    VkSampler sampNearest_ = VK_NULL_HANDLE, sampLinear_ = VK_NULL_HANDLE;
    struct TexEntry { VkImageView view; bool linear; VkImageLayout layout; VkDescriptorSet set; VkImageView opaqueView; uint64_t lastUse; };
    std::vector<TexEntry> texGraveyard_;   // se liberan cuando la GPU ya no las usa
    uint64_t frameCount_ = 0;
    void collectTextures();
    std::vector<TexEntry> texCache_;
    bool pendingGame_ = false;
    VkDescriptorSet pendingSet_ = VK_NULL_HANDLE;
    float pendingRect_[4] = {};

    // Frames por software
    VkBuffer stage_[kFrames] = {};
    VkDeviceMemory stageMem_[kFrames] = {};
    void* stageMap_[kFrames] = {};
    VkImage swImg_ = VK_NULL_HANDLE; VkDeviceMemory swMem_ = VK_NULL_HANDLE;
    VkFormat swFmt_ = VK_FORMAT_UNDEFINED;
    VkImageLayout swLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    static constexpr VkDeviceSize kStageSize = 1024 * 512 * 4;

    retro_hw_render_interface_vulkan iface_ = {};
};
