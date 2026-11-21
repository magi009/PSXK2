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

    // Texturas para la interfaz (portadas). Devuelve el ImTextureID (0 = error).
    uint64_t createTexture(const uint8_t* rgba, int w, int h);
    void destroyTexture(uint64_t id);
    unsigned generation() const { return gen_; }   // cambia cuando se recrea el dispositivo

    bool imguiInit();
    void imguiShutdown();
    void imguiNewFrame();
    void imguiRender(ImDrawData* dd);

    void resetCore() { hasCoreImg_ = false; histValid_ = false; fgValid_ = false; }

    // ---- Pantalla 120 Hz (vk_post.cpp)
    // mode: 0 = 60 Hz normal, 1 = VRR a la frecuencia del juego, 2 = BFI, 3 = frames intermedios.
    // Se llama cada frame: aplica las tasas a la ventana solo si cambian y comprueba si la pantalla va a 120 Hz.
    void updateRefresh(int mode, unsigned gameFps, bool vsync);
    bool doublePresent() const { return doublePresent_; }
    // ScaleFX (Sp00kyFox): suavizado de bordes para pixel art, 5 pasadas, salida 3x. Pensado para resolución interna 1x.
    void setScaleFx(bool on) { sfxOn_ = on; }
    // Escalador de píxel: 0 ninguno, 2 Scale2x, 3 Scale3x, 4 Eagle 2x
    void setPixelScaler(int mode) { pixMode_ = mode; }
    // Efecto de pantalla (0..4) y divisor de la resolución interna (para seguir las líneas originales)
    void setScreenFx(int effect, unsigned divisor) { fxEffect_ = effect; fxDiv_ = divisor ? divisor : 1; }   // 2 presentaciones por frame emulado
    int refreshMode() const { return refreshMode_; }
    unsigned presentHz() const { return doublePresent_ ? appliedFps_ * 2 : refreshMode_ == 1 ? appliedFps_ : 60; }
    void setInterp(bool live);                              // juego corriendo a velocidad normal, sin menú
    bool presentInterpLatest();                             // frame extra: el último frame real
    // Antialiasing de posprocesado: FXAA y/o TAA (temporal). El MSAA lo hace el núcleo.
    void setAntialiasing(bool fxaa, bool taa) { if (taa != aaTaa_) histValid_ = false; aaFxaa_ = fxaa; aaTaa_ = taa; }
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

    struct UserTex { VkImage img; VkDeviceMemory mem; VkImageView view; VkDescriptorSet set; uint64_t freeAt; };
    std::vector<UserTex> userTex_, userTexGrave_;
    unsigned gen_ = 1;
    void destroyUserTextures();

    // ---- Posprocesado (vk_post.cpp)
    bool ensurePost();
    void destroyPost();
    bool ensureHistory(uint32_t w, uint32_t h);
    void recordTaa(VkImageView cur, VkImageLayout curLayout, uint32_t w, uint32_t h, bool fxaa);
    VkDescriptorSet allocPostSet(VkImageView a, VkImageLayout la, VkImageView b, VkImageLayout lb, VkSampler s);
    void drawPendingGame();
    void beginUiPass();
    void endUiPass();
    void collectHistory();
    bool aaFxaa_ = false, aaTaa_ = false;
    bool postReady_ = false, postFailed_ = false, uiRendered_ = false;
    VkDescriptorSetLayout postDsl_ = VK_NULL_HANDLE;
    VkPipelineLayout postLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool postPool_[2] = {};
    VkRenderPass histPass_ = VK_NULL_HANDLE, postForPass_ = VK_NULL_HANDLE;
    VkPipeline pipePresent_ = VK_NULL_HANDLE, pipeTaa_ = VK_NULL_HANDLE;
    VkShaderModule vsMod_ = VK_NULL_HANDLE, fsPresent_ = VK_NULL_HANDLE, fsTaa_ = VK_NULL_HANDLE;
    struct Hist { VkImage img; VkDeviceMemory mem; VkImageView view; VkFramebuffer fb; uint64_t freeAt; };
    Hist hist_[2] = {};
    std::vector<Hist> histGrave_;
    uint32_t histW_ = 0, histH_ = 0;
    int histCur_ = 0;
    bool histValid_ = false;
    bool createTarget(Hist& t, uint32_t w, uint32_t h);
    bool createTargetFmt(Hist& t, uint32_t w, uint32_t h, VkFormat fmt, VkRenderPass rp);
    void recordPass(VkPipeline pipe, VkRenderPass rp, Hist& dst, uint32_t w, uint32_t h, VkImageView a, VkImageLayout la,
                    VkImageView b, VkImageLayout lb, float rx, float ry, float mode = 0.f);
    bool ensurePix(uint32_t w, uint32_t h, uint32_t f);
    Hist pix_ = {};
    uint32_t pixW_ = 0, pixH_ = 0;
    VkPipeline pipePix_ = VK_NULL_HANDLE;
    VkShaderModule fsPix_ = VK_NULL_HANDLE;
    int pixMode_ = 0, fxEffect_ = 0;
    unsigned fxDiv_ = 1;
    bool pixFailed_ = false;
    bool ensureSfx(uint32_t w, uint32_t h);
    void recordSfx(VkImageView src, VkImageLayout lay, uint32_t w, uint32_t h);
    Hist sfx_[5] = {};                  // métricas (16F) x2, datos x2, salida 3x
    uint32_t sfxW_ = 0, sfxH_ = 0;
    VkRenderPass sfxPass16_ = VK_NULL_HANDLE;
    VkPipeline pipeSfx_[5] = {};
    VkShaderModule fsSfx_[5] = {};
    bool sfxOn_ = false, sfxFailed_ = false;
    bool ensureFg(uint32_t w, uint32_t h);
    void recordCopy(VkImageView src, VkImageLayout lay, Hist& dst, uint32_t w, uint32_t h);
    int refreshMode_ = 0, appliedMode_ = -1;
    unsigned appliedFps_ = 0;
    bool ratesSet_ = false, doublePresent_ = false, interpLive_ = false;
    Hist fg_[2] = {};
    uint32_t fgW_ = 0, fgH_ = 0;
    int fgLatest_ = 0;
    bool fgValid_ = false;
    VkPipeline pipeBlend_ = VK_NULL_HANDLE, pipeCopy_ = VK_NULL_HANDLE;
    VkShaderModule fsBlend_ = VK_NULL_HANDLE;
    struct PendingPost { VkImageView view; VkImageLayout layout; bool linear; bool fxaa; float rcp[2]; int vx, vy, vw, vh;
                         VkImageView blendWith = VK_NULL_HANDLE;   // != null: mezcla 50 % (frame intermedio)
                         float effect = 0.f; float fxSize[2] = {0.f, 0.f}; };
    PendingPost post_ = {}, fgPost_ = {};
    bool pendingPost_ = false;

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
