#include "core_host.h"
#include "audio.h"
#include "config.h"
#include "input.h"
#include "log.h"
#include "paths.h"
#include "video.h"
#include <EGL/egl.h>
#include <libretro.h>
#define VK_USE_PLATFORM_VI_NN
#include <vulkan/vulkan.h>
#include <libretro_vulkan.h>
#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>

// Tipos de mando del núcleo Beetle PSX
#define PSX_DEV_DUALSHOCK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)

// ------------------------------------------------------------------ estado global (callbacks C)
static CoreHost* s_host;
static Video* s_video;
static Audio* s_audio;
static Input* s_input;
static retro_hw_render_callback s_hw = {};
static bool s_hwActive = false;
static retro_pixel_format s_pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static retro_system_av_info s_av = {};
static retro_disk_control_callback s_disk = {};
static bool s_hasDisk = false;
static const retro_hw_render_context_negotiation_interface_vulkan* s_negotiation = nullptr;
static bool s_videoDone = false;
static bool s_blockAudio = true;
static std::map<std::string, std::string> s_coreDefaults;  // valores por defecto del núcleo
static std::string s_valueBuf[256];
static unsigned s_valueIdx = 0;

// ------------------------------------------------------------------ callbacks
static void RETRO_CALLCONV core_log(enum retro_log_level, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[1024]; vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t n = strlen(buf); if (n && buf[n - 1] == '\n') buf[n - 1] = 0;
    LOGI("[core] %s", buf);
}

static uintptr_t RETRO_CALLCONV hw_get_fb() { return s_video->coreFbo(); }
static retro_proc_address_t RETRO_CALLCONV hw_get_proc(const char* sym) {
    return (retro_proc_address_t)eglGetProcAddress(sym);
}

static bool RETRO_CALLCONV set_rumble(unsigned port, enum retro_rumble_effect e, uint16_t strength) {
    s_input->rumble(port, e == RETRO_RUMBLE_STRONG, strength);
    return true;
}

static bool RETRO_CALLCONV env_cb(unsigned cmd, void* data) {
    Config& cfg = Config::get();
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((retro_log_callback*)data)->log = core_log; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char**)data = paths::bios.c_str(); return true;      // BIOS
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char**)data = paths::memcards.c_str(); return true;  // memcards .mcr
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool*)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        auto f = *(retro_pixel_format*)data;
        if (f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565) return false;
        s_pixfmt = f; return true;
    }
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *(unsigned*)data = s_video->isVulkan() ? RETRO_HW_CONTEXT_VULKAN : RETRO_HW_CONTEXT_OPENGL_CORE;
        return true;
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
        if (!s_video->isVulkan()) return false;
        *(const retro_hw_render_interface**)data = (const retro_hw_render_interface*)s_video->vkInterface();
        return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        auto* hw = (retro_hw_render_callback*)data;
        // OunceVK ofrece Vulkan y OpenGL; Direct3D no existe en esta plataforma
        bool vk = hw->context_type == RETRO_HW_CONTEXT_VULKAN;
        bool gl = hw->context_type == RETRO_HW_CONTEXT_OPENGL_CORE || hw->context_type == RETRO_HW_CONTEXT_OPENGL;
        if ((vk && !s_video->isVulkan()) || (gl && s_video->isVulkan()) || (!vk && !gl)) {
            LOGI("[host] contexto HW %d no coincide con el backend activo", hw->context_type);
            return false;
        }
        hw->get_current_framebuffer = hw_get_fb;
        hw->get_proc_address = hw_get_proc;
        s_hw = *hw; s_hwActive = true;
        LOGI("[host] HW render %s %u.%u", vk ? "Vulkan" : "GL", hw->version_major, hw->version_minor);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT: {
        auto* n = (retro_hw_render_context_negotiation_interface*)data;
        if (!s_video->isVulkan() || n->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN) return false;
        n->interface_version = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION;   // v2
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE: {
        auto* n = (const retro_hw_render_context_negotiation_interface_vulkan*)data;
        if (!n || n->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN) return false;
        s_negotiation = n;
        LOGI("[host] interfaz de negociación Vulkan v%u recibida", n->interface_version);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned*)data = 0; return true;   // el núcleo usará SET_VARIABLES (formato clásico)
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        for (auto* v = (const retro_variable*)data; v && v->key; v++) {
            // "Descripción; valor1|valor2|..." -> el primero es el valor por defecto
            const char* semi = strchr(v->value, ';');
            if (!semi) continue;
            std::string vals = semi + 1;
            while (!vals.empty() && vals[0] == ' ') vals.erase(0, 1);
            s_coreDefaults[v->key] = vals.substr(0, vals.find('|'));
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* v = (retro_variable*)data;
        const char* managed = cfg.coreValue(v->key);
        if (managed) { v->value = managed; return true; }
        auto it = s_coreDefaults.find(v->key);
        if (it == s_coreDefaults.end()) { v->value = nullptr; return false; }
        std::string& slot = s_valueBuf[s_valueIdx++ & 255];
        slot = it->second; v->value = slot.c_str();
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool*)data = cfg.consumeCoreDirty(); return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        s_av.geometry = *(retro_game_geometry*)data;
        s_video->setAspect(s_av.geometry.aspect_ratio > 0 ? s_av.geometry.aspect_ratio : 4.f / 3.f);
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        s_av = *(retro_system_av_info*)data;
        if (s_hwActive) s_video->ensureCoreFbo(s_av.geometry.max_width, s_av.geometry.max_height);
        s_audio->setInputRate(s_av.timing.sample_rate);
        return true;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        ((retro_rumble_interface*)data)->set_rumble_state = set_rumble; return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        s_disk = *(const retro_disk_control_callback*)data; s_hasDisk = true; return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(unsigned*)data = RETRO_LANGUAGE_SPANISH; return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        *(bool*)data = !s_blockAudio; return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        return true;
    default:
        return false;
    }
}

static void RETRO_CALLCONV video_cb(const void* data, unsigned w, unsigned h, size_t pitch) {
    s_videoDone = true;
    static unsigned n = 0;
    if (n < 5) { n++; LOGI("[host] video_cb %ux%u %s", w, h, !data ? "duplicado" : data == RETRO_HW_FRAME_BUFFER_VALID ? "HW (Vulkan)" : "software"); }
    if (!data) { s_video->redrawLast(); return; }                    // frame duplicado
    if (data == RETRO_HW_FRAME_BUFFER_VALID) s_video->presentHw(w, h);
    else s_video->presentSw(data, w, h, pitch, s_pixfmt == RETRO_PIXEL_FORMAT_XRGB8888);
}

static void RETRO_CALLCONV audio_cb(int16_t l, int16_t r) {
    int16_t f[2] = {l, r}; s_audio->push(f, 1, s_blockAudio);
}
static size_t RETRO_CALLCONV audio_batch_cb(const int16_t* d, size_t frames) {
    s_audio->push(d, frames, s_blockAudio); return frames;
}
static void RETRO_CALLCONV input_poll_cb() {}
static int16_t RETRO_CALLCONV input_state_cb(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    return s_input->state(port, dev, idx, id);
}

// ------------------------------------------------------------------ CoreHost
CoreHost::CoreHost(Video& v, Audio& a, Input& i) : video_(v), audio_(a), input_(i) {
    s_host = this; s_video = &v; s_audio = &a; s_input = &i;
}

void CoreHost::init() {
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();
    retro_system_info si = {};
    retro_get_system_info(&si);
    LOGI("[host] núcleo %s %s", si.library_name, si.library_version);
}

void CoreHost::deinit() { unloadGame(); retro_deinit(); }

void CoreHost::applyControllers() {
    for (unsigned p = 0; p < 2; p++) {
        const char* t = Config::get().value(p == 0 ? "pad1" : "pad2");
        unsigned dev = !strcmp(t, "dualshock") ? PSX_DEV_DUALSHOCK : !strcmp(t, "digital") ? RETRO_DEVICE_JOYPAD : RETRO_DEVICE_NONE;
        retro_set_controller_port_device(p, dev);
    }
}

bool CoreHost::loadGame(const std::string& path, std::string& error) {
    unloadGame();
    if (!paths::missingBios().empty()) {
        error = "Falta la BIOS. Copia scph5501.bin (USA), scph5500.bin (JAP) o scph5502.bin (EUR) en "
                + paths::bios;
        return false;
    }
    stem_ = paths::stem(path); path_ = path;
    Config::get().loadGame(stem_);
    s_hwActive = false; s_hw = {}; s_negotiation = nullptr;

    retro_game_info gi = {};
    gi.path = path_.c_str();
    if (!retro_load_game(&gi)) {
        error = "El núcleo no pudo cargar el juego (revisa logs/psx-k2.log)";
        Config::get().unloadGame();
        return false;
    }
    retro_get_system_av_info(&s_av);
    LOGI("[host] %s: %ux%u máx %ux%u, %.3f fps, %.0f Hz", stem_.c_str(), s_av.geometry.base_width,
         s_av.geometry.base_height, s_av.geometry.max_width, s_av.geometry.max_height, s_av.timing.fps, s_av.timing.sample_rate);
    video_.setAspect(s_av.geometry.aspect_ratio > 0 ? s_av.geometry.aspect_ratio : 4.f / 3.f);
    audio_.setInputRate(s_av.timing.sample_rate);
    if (s_hwActive) {
        video_.ensureCoreFbo(s_av.geometry.max_width, s_av.geometry.max_height);
        // Vulkan: el núcleo crea su dispositivo durante la negociación, antes de context_reset
        if (video_.isVulkan() && s_negotiation) video_.vkAdoptCoreDevice(s_negotiation);
        else if (video_.isVulkan()) LOGI("[host] el núcleo no envió interfaz de negociación");
        if (s_hw.context_reset) s_hw.context_reset();
        LOGI("[host] context_reset hecho");
    }
    applyControllers();

    // Memory card ranura 1 en formato libretro (.srm)
    size_t sz = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void* mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    std::vector<unsigned char> buf;
    if (sz && mem && paths::readFile(paths::memcards + "/" + stem_ + ".srm", buf) && buf.size() == sz)
        memcpy(mem, buf.data(), sz);
    if (sz && mem) srmShadow_.assign((unsigned char*)mem, (unsigned char*)mem + sz);
    loaded_ = true;
    if (Config::get().isOn("cpu_boost")) appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    return true;
}

void CoreHost::flushMemcard(bool force) {
    if (!loaded_) return;
    size_t sz = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    auto* mem = (unsigned char*)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!sz || !mem) return;
    if (!force) {
        int every = atoi(Config::get().value("autosave"));
        if (every == 0) return;
        unsigned long long now = armGetSystemTick();
        if (now - lastSrmCheck_ < armNsToTicks((u64)every * 1000000000ULL)) return;
        lastSrmCheck_ = now;
    }
    if (srmShadow_.size() == sz && !memcmp(srmShadow_.data(), mem, sz)) return;
    if (paths::writeFile(paths::memcards + "/" + stem_ + ".srm", mem, sz)) {
        srmShadow_.assign(mem, mem + sz);
        LOGI("[host] memcard guardada");
    }
}

void CoreHost::unloadGame() {
    if (!loaded_) return;
    flushMemcard(true);
    if (s_hwActive && s_hw.context_destroy) s_hw.context_destroy();
    video_.vkReleaseCoreDevice();          // destroy_device del núcleo + dispositivo propio para el menú
    retro_unload_game();
    Config::get().saveGame();
    Config::get().unloadGame();
    s_hasDisk = false; s_hwActive = false;
    video_.resetCoreImage();
    loaded_ = false;
    appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
}

void CoreHost::reset() { if (loaded_) retro_reset(); }

void CoreHost::runFrame(bool fastForward) {
    s_blockAudio = !fastForward && Config::get().isOn("audio_sync");
    int n = fastForward ? 3 : 1;
    for (int i = 0; i < n; i++) {
        s_videoDone = false;
        retro_run();
        if (!s_videoDone) video_.redrawLast();
    }
    flushMemcard(false);

    frames_++;
    unsigned long long now = armGetSystemTick();
    if (!fpsTick_) fpsTick_ = now;
    double secs = armTicksToNs(now - fpsTick_) / 1e9;
    if (secs >= 1.0) { fps_ = (float)(frames_ * n / secs); frames_ = 0; fpsTick_ = now; }
}

std::string CoreHost::statePath(int slot) const {
    return paths::states + "/" + stem_ + ".state" + std::to_string(slot);
}
bool CoreHost::stateExists(int slot) const { return loaded_ && paths::exists(statePath(slot)); }

bool CoreHost::saveState(int slot) {
    if (!loaded_) return false;
    size_t sz = retro_serialize_size();
    std::vector<unsigned char> buf(sz);
    if (!sz || !retro_serialize(buf.data(), sz)) return false;
    return paths::writeFile(statePath(slot), buf.data(), sz);
}

bool CoreHost::loadState(int slot) {
    std::vector<unsigned char> buf;
    if (!loaded_ || !paths::readFile(statePath(slot), buf)) return false;
    return retro_unserialize(buf.data(), buf.size());
}

unsigned CoreHost::discCount() const { return s_hasDisk && s_disk.get_num_images ? s_disk.get_num_images() : 1; }
unsigned CoreHost::discIndex() const { return s_hasDisk && s_disk.get_image_index ? s_disk.get_image_index() : 0; }

bool CoreHost::swapDisc(unsigned index) {
    if (!s_hasDisk || index >= discCount()) return false;
    s_disk.set_eject_state(true);
    bool ok = s_disk.set_image_index(index);
    s_disk.set_eject_state(false);
    LOGI("[host] disco %u/%u %s", index + 1, discCount(), ok ? "insertado" : "falló");
    return ok;
}

bool CoreHost::takeScreenshot(std::string& out) {
    time_t t = time(nullptr);
    char ts[32]; strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", localtime(&t));
    out = paths::screenshots + "/" + stem_ + "-" + ts + ".bmp";
    return video_.screenshot(out);
}
