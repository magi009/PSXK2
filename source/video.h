#pragma once
#include <glad/glad.h>
#include <cstdint>
#include <string>

class VkPresenter;
struct ImDrawData;
struct retro_hw_render_interface_vulkan;
struct retro_hw_render_context_negotiation_interface_vulkan;

// Backend de vídeo con dos caminos (se elige al arrancar con la opción "Renderizador"):
//  - Vulkan 1.4 (OunceVK): el núcleo usa su renderer Vulkan; la salida se escala con blit.
//  - OpenGL 4.3 core (OunceVK EGL): el núcleo usa su renderer GL; escaladores por shader.
// - El núcleo renderiza en un FBO propio (escalado interno 1x..8x).
// - Una pasada final aplica el escalador elegido y dibuja al framebuffer de pantalla.
class Video {
public:
    bool init();
    void shutdown();

    // Ajusta salida (720p/1080p/4K) según opción y modo portátil/dock
    void applyOutputMode();
    int  screenW() const { return scrW_; }
    int  screenH() const { return scrH_; }

    // --- Render por hardware (contexto compartido con el núcleo)
    void ensureCoreFbo(unsigned w, unsigned h);
    uintptr_t coreFbo() const { return fbo_; }

    // --- Frames
    void presentHw(unsigned w, unsigned h);                       // frame válido en FBO
    void presentSw(const void* data, unsigned w, unsigned h, size_t pitch, bool xrgb8888);
    void redrawLast();                                            // frame duplicado / menú
    void clearScreen();
    void swap();
    void beginFrame();                                            // Vulkan: adquiere imagen

    // Pantalla 120 Hz (solo Vulkan). updateRefresh() una vez por frame con los fps nominales del juego;
    // setLive() antes de emular: true si el juego corre a velocidad normal y sin menú;
    // extraPresent() después de swap(): segunda imagen del frame (negra, intermedia o repetida).
    void updateRefresh(double gameFps);
    void setLive(bool live);
    void extraPresent();
    const char* refreshStatus() const;   // texto para Información

    bool isVulkan() const { return vk_ != nullptr; }
    retro_hw_render_interface_vulkan* vkInterface();
    void resetCoreImage();
    void vkReleaseCoreDevice();
    bool vkAdoptCoreDevice(const retro_hw_render_context_negotiation_interface_vulkan* neg);

    // ImGui (backend según API)
    bool imguiInit();
    void imguiShutdown();
    void imguiNewFrame();
    void imguiRender(ImDrawData* dd);
    void addGameLayer();

    // Texturas de la interfaz (portadas): RGBA8. Devuelven/aceptan un ImTextureID como entero.
    uint64_t createTexture(const uint8_t* rgba, int w, int h);
    void destroyTexture(uint64_t id);
    unsigned textureGeneration() const;   // si cambia, todas las texturas anteriores dejaron de existir          // Vulkan: añade la imagen del juego al fondo del frame de ImGui

    void setAspect(float coreAspect) { coreAspect_ = coreAspect; }
    bool screenshot(const std::string& path);

private:
    void createSurface();
    void destroySurface();
    void buildShaders();
    void computeViewport(unsigned w, unsigned h);
    bool linearFilter() const;
    void draw(GLuint tex, unsigned w, unsigned h, unsigned texW, unsigned texH, bool flip);

    VkPresenter* vk_ = nullptr;
    int scrW_ = 1280, scrH_ = 720;
    int reqH_ = 0;
    GLuint fbo_ = 0, fboTex_ = 0, fboDepth_ = 0;
    unsigned fboW_ = 0, fboH_ = 0;
    GLuint swTex_ = 0; unsigned swW_ = 0, swH_ = 0;
    GLuint vao_ = 0;
    GLuint progs_[6] = {};
    float coreAspect_ = 4.0f / 3.0f;
    bool live_ = false;

    enum { LAST_NONE, LAST_HW, LAST_SW } last_ = LAST_NONE;
    unsigned lastW_ = 0, lastH_ = 0;
    size_t lastPitch_ = 0; bool lastXrgb_ = true;
    int vpX_ = 0, vpY_ = 0, vpW_ = 0, vpH_ = 0;
};
