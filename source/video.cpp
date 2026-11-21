#include "video.h"
#include "vk_presenter.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "config.h"
#include "log.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

static EGLDisplay s_dpy = EGL_NO_DISPLAY;
static EGLContext s_ctx = EGL_NO_CONTEXT;
static EGLSurface s_surf = EGL_NO_SURFACE;
static EGLConfig  s_cfg;

// ------------------------------------------------------------------ shaders
static const char* kVS = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

static const char* kFSHead = R"(#version 330 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uTex;
uniform vec2 uTexSize;   // tamaño total de la textura
uniform vec2 uSrcSize;   // tamaño útil del frame
uniform vec2 uOutSize;   // tamaño del viewport de salida
uniform float uFlip;
vec2 srcUV() {
    vec2 uv = vec2(vUV.x, mix(vUV.y, 1.0 - vUV.y, uFlip));
    return uv * (uSrcSize / uTexSize);
}
)";

static const char* kFS[5] = {
// 0 nearest
R"(void main(){ frag = vec4(texture(uTex, srcUV()).rgb, 1.0); })",
// 1 bilinear
R"(void main(){ frag = vec4(texture(uTex, srcUV()).rgb, 1.0); })",
// 2 sharp bilinear (escalado entero nítido + interpolación solo en el borde)
R"(void main(){
    vec2 texel = srcUV() * uTexSize;
    vec2 scale = max(floor(uOutSize / uSrcSize), vec2(1.0));
    vec2 tf = floor(texel);
    vec2 cd = fract(texel) - 0.5;
    vec2 rr = 0.5 - 0.5 / scale;
    vec2 f = (cd - clamp(cd, -rr, rr)) * scale + 0.5;
    frag = vec4(texture(uTex, (tf + f) / uTexSize).rgb, 1.0);
})",
// 3 bicúbico Catmull-Rom (9 lecturas bilineales)
R"(void main(){
    vec2 samplePos = srcUV() * uTexSize;
    vec2 tp1 = floor(samplePos - 0.5) + 0.5;
    vec2 f = samplePos - tp1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 o12 = w2 / w12;
    vec2 t0 = (tp1 - 1.0) / uTexSize, t3 = (tp1 + 2.0) / uTexSize, t12 = (tp1 + o12) / uTexSize;
    vec3 c = vec3(0.0);
    c += texture(uTex, vec2(t0.x,  t0.y)).rgb  * w0.x  * w0.y;
    c += texture(uTex, vec2(t12.x, t0.y)).rgb  * w12.x * w0.y;
    c += texture(uTex, vec2(t3.x,  t0.y)).rgb  * w3.x  * w0.y;
    c += texture(uTex, vec2(t0.x,  t12.y)).rgb * w0.x  * w12.y;
    c += texture(uTex, vec2(t12.x, t12.y)).rgb * w12.x * w12.y;
    c += texture(uTex, vec2(t3.x,  t12.y)).rgb * w3.x  * w12.y;
    c += texture(uTex, vec2(t0.x,  t3.y)).rgb  * w0.x  * w3.y;
    c += texture(uTex, vec2(t12.x, t3.y)).rgb  * w12.x * w3.y;
    c += texture(uTex, vec2(t3.x,  t3.y)).rgb  * w3.x  * w3.y;
    frag = vec4(clamp(c, 0.0, 1.0), 1.0);
})",
// 4 CRT scanlines + máscara de apertura simple (la variante 5, FXAA, va al final)
R"(void main(){
    vec2 uv = srcUV();
    vec3 c = texture(uTex, uv).rgb;
    float lines = 240.0;
    float y = fract(vUV.y * lines);
    float scan = 0.65 + 0.35 * sin(y * 3.14159);
    float m = mod(gl_FragCoord.x, 3.0);
    vec3 mask = m < 1.0 ? vec3(1.0, 0.85, 0.85) : (m < 2.0 ? vec3(0.85, 1.0, 0.85) : vec3(0.85, 0.85, 1.0));
    frag = vec4(pow(c * scan * mask * 1.15, vec3(0.95)), 1.0);
})",
};

// 5 FXAA (versión de consola de Timothy Lottes) en espacio de la imagen de origen
static const char* kFSFxaa = R"(
float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main(){
    vec2 uv = srcUV(); vec2 r = 1.0 / uTexSize;
    vec3 nw = texture(uTex, uv + vec2(-1,-1) * r).rgb, ne = texture(uTex, uv + vec2(1,-1) * r).rgb;
    vec3 sw = texture(uTex, uv + vec2(-1, 1) * r).rgb, se = texture(uTex, uv + vec2(1, 1) * r).rgb;
    vec3 m = texture(uTex, uv).rgb;
    float a = lum(nw), b = lum(ne), c = lum(sw), d = lum(se), e = lum(m);
    float mn = min(e, min(min(a, b), min(c, d))), mx = max(e, max(max(a, b), max(c, d)));
    vec2 dir = vec2(-((a + b) - (c + d)), ((a + c) - (b + d)));
    float red = max((a + b + c + d) * 0.03125, 1.0 / 128.0);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + red), vec2(-8.0), vec2(8.0)) * r;
    vec3 A = 0.5 * (texture(uTex, uv + dir * (1.0/3.0 - 0.5)).rgb + texture(uTex, uv + dir * (2.0/3.0 - 0.5)).rgb);
    vec3 B = A * 0.5 + 0.25 * (texture(uTex, uv - dir * 0.5).rgb + texture(uTex, uv + dir * 0.5).rgb);
    float lb = lum(B);
    frag = vec4((lb < mn || lb > mx) ? A : B, 1.0);
})";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log); LOGI("[video] shader: %s", log); }
    return s;
}

void Video::buildShaders() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVS);
    for (int i = 0; i < 6; i++) {
        std::string src = std::string(kFSHead) + (i < 5 ? kFS[i] : kFSFxaa);
        GLuint fs = compile(GL_FRAGMENT_SHADER, src.c_str());
        progs_[i] = glCreateProgram();
        glAttachShader(progs_[i], vs); glAttachShader(progs_[i], fs);
        glLinkProgram(progs_[i]);
        glDeleteShader(fs);
    }
    glDeleteShader(vs);
    glGenVertexArrays(1, &vao_);
}

// ------------------------------------------------------------------ EGL
static int wantedHeight() {
    const char* v = Config::get().value("output_res");
    if (!strcmp(v, "auto")) return appletGetOperationMode() == AppletOperationMode_Console ? 2160 : 1080;
    return atoi(v);
}

bool Video::init() {
    if (!strcmp(Config::get().value("renderer"), "hardware_vk")) {
        vk_ = new VkPresenter();
        int h = wantedHeight();
        if (vk_->init(h * 16 / 9, h, Config::get().isOn("vsync"))) {
            scrW_ = vk_->width(); scrH_ = vk_->height(); reqH_ = h;
            LOGI("[video] backend Vulkan %dx%d", scrW_, scrH_);
            return true;
        }
        LOGI("[video] Vulkan falló, usando OpenGL");
        vk_->shutdown(); delete vk_; vk_ = nullptr;
        Config::get().setIndex("renderer", 1);   // software + salida OpenGL
    }
    s_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!s_dpy || !eglInitialize(s_dpy, nullptr, nullptr)) { LOGI("[video] eglInitialize falló"); return false; }
    eglBindAPI(EGL_OPENGL_API);
    static const EGLint attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                  EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLint n;
    if (!eglChooseConfig(s_dpy, attr, &s_cfg, 1, &n) || n == 0) { LOGI("[video] sin EGLConfig"); return false; }
    static const EGLint ctxAttr[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                                     EGL_CONTEXT_MAJOR_VERSION_KHR, 4, EGL_CONTEXT_MINOR_VERSION_KHR, 3, EGL_NONE};
    s_ctx = eglCreateContext(s_dpy, s_cfg, EGL_NO_CONTEXT, ctxAttr);
    if (!s_ctx) { LOGI("[video] eglCreateContext falló"); return false; }
    applyOutputMode();
    if (!gladLoadGL()) { LOGI("[video] gladLoadGL falló"); return false; }
    LOGI("[video] GL %s | %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    buildShaders();
    return true;
}

void Video::destroySurface() {
    if (s_surf != EGL_NO_SURFACE) {
        eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, s_ctx);
        eglDestroySurface(s_dpy, s_surf);
        s_surf = EGL_NO_SURFACE;
    }
}

void Video::createSurface() {
    NWindow* win = nwindowGetDefault();
    int w = scrH_ * 16 / 9;
    if (R_FAILED(nwindowSetDimensions(win, w, scrH_))) {
        LOGI("[video] el sistema rechazó %dx%d, usando 1920x1080", w, scrH_);
        scrH_ = 1080; w = 1920;
        nwindowSetDimensions(win, w, scrH_);
    }
    scrW_ = w;
    s_surf = eglCreateWindowSurface(s_dpy, s_cfg, win, nullptr);
    eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx);
    eglSwapInterval(s_dpy, Config::get().isOn("vsync") ? 1 : 0);
    LOGI("[video] salida %dx%d", scrW_, scrH_);
}

void Video::applyOutputMode() {
    // Switch 2: pantalla portátil 1080p; dock hasta 4K
    int h = wantedHeight();
    if (vk_) {
        if (h == reqH_) return;
        reqH_ = h;
        vk_->resize(h * 16 / 9, h, Config::get().isOn("vsync"));
        scrW_ = vk_->width(); scrH_ = vk_->height();
        return;
    }
    if (h == reqH_ && s_surf != EGL_NO_SURFACE) {
        eglSwapInterval(s_dpy, Config::get().isOn("vsync") ? 1 : 0);
        return;
    }
    reqH_ = h; scrH_ = h;
    destroySurface();
    createSurface();
}

void Video::shutdown() {
    if (vk_) { vk_->shutdown(); delete vk_; vk_ = nullptr; return; }
    if (s_dpy) {
        eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_ctx) eglDestroyContext(s_dpy, s_ctx);
        if (s_surf) eglDestroySurface(s_dpy, s_surf);
        eglTerminate(s_dpy);
    }
    s_dpy = EGL_NO_DISPLAY;
}

// ------------------------------------------------------------------ FBO del núcleo
void Video::ensureCoreFbo(unsigned w, unsigned h) {
    if (vk_) return;
    if (fbo_ && w <= fboW_ && h <= fboH_) return;
    if (fbo_) { glDeleteFramebuffers(1, &fbo_); glDeleteTextures(1, &fboTex_); glDeleteRenderbuffers(1, &fboDepth_); }
    fboW_ = w; fboH_ = h;
    glGenTextures(1, &fboTex_);
    glBindTexture(GL_TEXTURE_2D, fboTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenRenderbuffers(1, &fboDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, fboDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fboDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        LOGI("[video] FBO %ux%u incompleto (¿sin memoria? baja la resolución interna)", w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOGI("[video] FBO del núcleo %ux%u", w, h);
}

// ------------------------------------------------------------------ dibujo
void Video::computeViewport(unsigned w, unsigned h) {
    Config& c = Config::get();
    // Rectángulo de destino según el modo de pantalla
    const char* mode = c.value("screen_mode");
    if (!strcmp(mode, "fullscreen") || !strcmp(mode, "exclusive")) {
        // Completa: toda la pantalla. Exclusiva: igual, pero el núcleo ya renderiza en panorámico
        vpW_ = scrW_; vpH_ = scrH_;
    } else if (!strcmp(mode, "integer")) {
        unsigned nativeH = h; while (nativeH > 480) nativeH /= 2;
        int k = std::max(1, scrH_ / (int)nativeH);
        vpH_ = k * nativeH; vpW_ = (int)(vpH_ * coreAspect_);
        if (vpW_ > scrW_) { vpW_ = scrW_; vpH_ = (int)(scrW_ / coreAspect_); }
    } else {
        vpH_ = scrH_; vpW_ = (int)(scrH_ * coreAspect_);
        if (vpW_ > scrW_) { vpW_ = scrW_; vpH_ = (int)(scrW_ / coreAspect_); }
    }
    vpX_ = (scrW_ - vpW_) / 2; vpY_ = (scrH_ - vpH_) / 2;

}

bool Video::linearFilter() const {
    const char* us = Config::get().value("upscale_shader");
    return strcmp(us, "nearest") != 0;   // tras ScaleFX/Scale2x la imagen ya es 2-3x: un bilineal suave evita saltos
}

void Video::draw(GLuint tex, unsigned w, unsigned h, unsigned texW, unsigned texH, bool flip) {
    Config& c = Config::get();
    const char* sh = c.value("upscale_shader");
    // En OpenGL: ScaleFX -> Sharp Bilinear; Scale2x/3x/Eagle -> Ninguno; Scanlines/CRT -> shader CRT
    int p = !strcmp(sh, "bilinear") ? 1 : !strcmp(sh, "sharp") || !strcmp(sh, "scalefx") ? 2 : !strcmp(sh, "bicubic") ? 3 : 0;
    const char* fxv = c.value("screen_fx");
    if (!strcmp(fxv, "scanlines") || !strcmp(fxv, "crt")) p = 4;
    if (strcmp(c.value("aa_post"), "off")) p = 5;   // en OpenGL: FXAA (el TAA solo existe en Vulkan)

    computeViewport(w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, scrW_, scrH_);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vpX_, vpY_, vpW_, vpH_);

    glUseProgram(progs_[p]);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint filt = p == 0 ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(glGetUniformLocation(progs_[p], "uTex"), 0);
    glUniform2f(glGetUniformLocation(progs_[p], "uTexSize"), (float)texW, (float)texH);
    glUniform2f(glGetUniformLocation(progs_[p], "uSrcSize"), (float)w, (float)h);
    glUniform2f(glGetUniformLocation(progs_[p], "uOutSize"), (float)vpW_, (float)vpH_);
    glUniform1f(glGetUniformLocation(progs_[p], "uFlip"), flip ? 1.f : 0.f);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    glViewport(0, 0, scrW_, scrH_);
}

void Video::presentHw(unsigned w, unsigned h) {
    last_ = LAST_HW; lastW_ = w; lastH_ = h;
    if (vk_) {
        const char* aa = Config::get().value("aa_post");
        vk_->setAntialiasing(!strcmp(aa, "fxaa") || !strcmp(aa, "fxaa_taa"), !strcmp(aa, "taa") || !strcmp(aa, "fxaa_taa"));
        Config& cf = Config::get();
        const char* us = cf.value("upscale_shader");
        vk_->setScaleFx(!strcmp(us, "scalefx"));
        vk_->setPixelScaler(!strcmp(us, "scale2x") ? 2 : !strcmp(us, "scale3x") ? 3 : !strcmp(us, "eagle2x") ? 4 : 0);
        unsigned div = !strcmp(cf.value("renderer"), "hardware_vk") ? 1u << cf.index("internal_res") : 1u;
        vk_->setScreenFx(cf.index("screen_fx"), div);
        computeViewport(w, h);
        vk_->presentCore(w, h, vpX_, vpY_, vpW_, vpH_, linearFilter());
        return;
    }
    draw(fboTex_, w, h, fboW_, fboH_, false);   // el núcleo usa origen abajo-izquierda
}

void Video::presentSw(const void* data, unsigned w, unsigned h, size_t pitch, bool xrgb8888) {
    if (vk_) {
        last_ = LAST_SW; lastW_ = w; lastH_ = h; lastPitch_ = pitch; lastXrgb_ = xrgb8888;
        computeViewport(w, h);
        vk_->presentSw(data, w, h, pitch, xrgb8888, vpX_, vpY_, vpW_, vpH_, linearFilter());
        return;
    }
    if (!swTex_) glGenTextures(1, &swTex_);
    glBindTexture(GL_TEXTURE_2D, swTex_);
    if (w > swW_ || h > swH_) {
        swW_ = std::max(w, 1024u); swH_ = std::max(h, 512u);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, swW_, swH_, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(pitch / (xrgb8888 ? 4 : 2)));
    if (xrgb8888) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
    else          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    last_ = LAST_SW; lastW_ = w; lastH_ = h;
    draw(swTex_, w, h, swW_, swH_, true);
}

void Video::redrawLast() {
    if (vk_) {
        computeViewport(lastW_, lastH_);
        if (last_ == LAST_HW) vk_->presentCore(lastW_, lastH_, vpX_, vpY_, vpW_, vpH_, linearFilter());
        else if (last_ == LAST_SW) vk_->presentSw(nullptr, lastW_, lastH_, lastPitch_, lastXrgb_, vpX_, vpY_, vpW_, vpH_, linearFilter());
        return;
    }
    if (last_ == LAST_HW) draw(fboTex_, lastW_, lastH_, fboW_, fboH_, false);
    else if (last_ == LAST_SW) draw(swTex_, lastW_, lastH_, swW_, swH_, true);
    else clearScreen();
}

void Video::clearScreen() {
    if (vk_) return;   // endFrame() limpia si no se dibujó nada
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, scrW_, scrH_);
    glClearColor(0.06f, 0.06f, 0.09f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Video::swap() { if (vk_) vk_->endFrame(); else eglSwapBuffers(s_dpy, s_surf); }
void Video::beginFrame() { if (vk_) vk_->beginFrame(); }

void Video::updateRefresh(double gameFps) {
    if (!vk_) return;
    Config& c = Config::get();
    int mode = c.index("display_120");
    vk_->updateRefresh(mode, (unsigned)(gameFps + 0.5), c.isOn("vsync"));
}

void Video::setLive(bool live) {
    live_ = live;
    if (vk_) vk_->setInterp(live);
}

unsigned Video::presentHz() const { return vk_ ? vk_->presentHz() : 60; }

const char* Video::refreshStatus() const {
    if (!vk_) return "60 Hz";
    bool dbl = vk_->doublePresent();
    switch (vk_->refreshMode()) {
    case 1: return "VRR a la frecuencia del juego";
    case 2: return dbl ? "120 Hz con negro intercalado (BFI)" : "BFI en espera: la pantalla va a 60 Hz";
    case 3: return dbl ? "120 Hz con frames intermedios" : "Frames intermedios en espera: la pantalla va a 60 Hz";
    default: return "60 Hz";
    }
}

void Video::extraPresent() {
    if (!vk_ || !vk_->doublePresent()) return;
    ImDrawData* dd = ImGui::GetDrawData();
    bool ui = dd && dd->Valid;
    vk_->beginFrame();
    if (live_ && vk_->refreshMode() == 2) {
        // BFI: imagen negra (endFrame limpia si no se dibujó nada); el OSD parpadea con ella
    } else if (live_ && vk_->refreshMode() == 3 && vk_->presentInterpLatest()) {
        if (ui) vk_->imguiRender(dd);          // el frame real, con el OSD encima
    } else {
        redrawLast();                          // menú, avance rápido o sin juego: se repite la imagen
        if (ui) vk_->imguiRender(dd);
    }
    vk_->endFrame();
}
retro_hw_render_interface_vulkan* Video::vkInterface() { return vk_ ? vk_->iface() : nullptr; }
void Video::vkReleaseCoreDevice() { if (vk_) vk_->releaseCoreDevice(); }
bool Video::vkAdoptCoreDevice(const retro_hw_render_context_negotiation_interface_vulkan* neg) {
    return vk_ && vk_->adoptCoreDevice(neg);
}
void Video::resetCoreImage() { if (vk_) vk_->resetCore(); last_ = LAST_NONE; }

bool Video::imguiInit() { return vk_ ? vk_->imguiInit() : ImGui_ImplOpenGL3_Init("#version 330 core"); }
void Video::imguiShutdown() { if (vk_) vk_->imguiShutdown(); else ImGui_ImplOpenGL3_Shutdown(); }
void Video::imguiNewFrame() { if (vk_) vk_->imguiNewFrame(); else ImGui_ImplOpenGL3_NewFrame(); }
uint64_t Video::createTexture(const uint8_t* rgba, int w, int h) {
    if (vk_) return vk_->createTexture(rgba, w, h);
    GLuint t; glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
void Video::destroyTexture(uint64_t id) {
    if (vk_) { vk_->destroyTexture(id); return; }
    GLuint t = (GLuint)id; glDeleteTextures(1, &t);
}
unsigned Video::textureGeneration() const { return vk_ ? vk_->generation() : 1; }

void Video::addGameLayer() { if (vk_) vk_->addGameLayer(); }
void Video::imguiRender(ImDrawData* dd) {
    if (vk_) vk_->imguiRender(dd); else ImGui_ImplOpenGL3_RenderDrawData(dd);
}

// ------------------------------------------------------------------ captura BMP 24 bits
bool Video::screenshot(const std::string& path) {
    if (vk_) return false;   // pendiente: copia del swapchain a buffer en Vulkan
    if (vpW_ <= 0 || vpH_ <= 0) return false;
    std::vector<unsigned char> px(vpW_ * vpH_ * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(vpX_, vpY_, vpW_, vpH_, GL_BGR, GL_UNSIGNED_BYTE, px.data());
    int rowPad = (4 - (vpW_ * 3) % 4) % 4;
    uint32_t dataSize = (vpW_ * 3 + rowPad) * vpH_;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) { memcpy(hdr + off, &v, 4); };
    put32(2, 54 + dataSize); put32(10, 54); put32(14, 40); put32(18, vpW_); put32(22, vpH_);
    hdr[26] = 1; hdr[28] = 24; put32(34, dataSize);
    fwrite(hdr, 1, 54, f);
    static const unsigned char pad[3] = {};
    for (int y = 0; y < vpH_; y++) {      // BMP va de abajo hacia arriba, igual que glReadPixels
        fwrite(&px[y * vpW_ * 3], 1, vpW_ * 3, f);
        fwrite(pad, 1, rowPad, f);
    }
    fclose(f);
    return true;
}
