#include "config.h"
#include "paths.h"
#include <cstdio>
#include <cstring>

static const std::vector<OptionChoice> OFFON = {{"Desactivado", "disabled"}, {"Activado", "enabled"}};
static const std::vector<OptionChoice> ONOFF = {{"Activado", "enabled"}, {"Desactivado", "disabled"}};

#define C_VIDEO   "Video"
#define C_3D      "Mejoras 3D (PGXP)"
#define C_TEX     "Texturas y filtros"
#define C_CPU     "CPU y rendimiento"
#define C_CD      "CD y BIOS"
#define C_MC      "Memory Cards"
#define C_INPUT   "Controles"
#define C_SYS     "Sistema"
#define C_AA      "Antialiasing"

static void assignFlags(std::vector<OptionDef>& defs);

Config& Config::get() { static Config c; return c; }

Config::Config() {
    defs_ = {
    // ---------------------------------------------------------------- VIDEO
    {"gfx_preset", "Perfil gráfico", C_VIDEO, nullptr,
        {{"Personalizado", "custom"}, {"Rendimiento", "performance"}, {"Equilibrado (recomendado)", "balanced"},
         {"Calidad máxima", "quality"}}, 2,
        "Ajusta de un toque las mejoras gráficas. Rendimiento: 2x, sin filtros. Equilibrado: 4x, PGXP, 32 bpp. "
        "Calidad máxima: 8x, PGXP completo, xBR y MSAA (la protección de rendimiento la bajará si hace falta). "
        "Si cambias una opción a mano pasa a Personalizado.", false},
    {"renderer", "Renderizador", C_VIDEO, "beetle_psx_hw_renderer",
        {{"Vulkan 1.4 (recomendado)", "hardware_vk"}, {"Software + salida OpenGL", "software"}}, 0,
        "Vulkan es el más rápido en Switch 2 y permite escalado interno, PGXP con perspectiva y MSAA. "
        "Software es exacto pero a resolución nativa; la imagen se muestra con OpenGL y todos sus "
        "escaladores (Sharp, Bicúbico, CRT). Cambiar de modo requiere reiniciar el emulador.", true},
    {"internal_res", "Resolución interna", C_VIDEO, "beetle_psx_hw_internal_resolution",
        {{"1x  nativa (320x240)", "1x(native)"}, {"2x  (640x480)", "2x"}, {"4x  (1280x960 ~1080p)", "4x"},
         {"8x  (2560x1920 ~4K)", "8x"}}, 2,
        "Multiplica la resolución de render del GPU de PSX. 8x supera la altura de 2160p (4K). "
        "Requiere mucho GPU: si baja de 60 fps usa 4x.", false},
    {"output_res", "Resolución de salida", C_VIDEO, nullptr,
        {{"Automática (según modo)", "auto"}, {"720p", "720"}, {"1080p", "1080"}, {"1440p", "1440"}, {"2160p (4K)", "2160"}}, 0,
        "Tamaño del framebuffer final. Automática: 1080p en portátil y 2160p (4K) en el dock. Si el "
        "sistema rechaza la resolución se vuelve a 1080p (ver log).", false},
    {"screen_mode", "Modo de pantalla", C_VIDEO, nullptr,
        {{"Pantalla original (4:3)", "original"}, {"Pantalla completa", "fullscreen"},
         {"Pantalla completa exclusiva", "exclusive"}, {"Escalado entero (píxel perfecto)", "integer"}}, 0,
        "Original: 4:3 como la PlayStation, con bandas a los lados. Completa: estira la imagen a toda la "
        "pantalla. Exclusiva: los juegos 3D se renderizan de verdad en panorámico (sin deformar) y se ocultan "
        "FPS y avisos; en 2D puede mostrar bordes extra.", false},
    {"upscale_shader", "Filtro de escalado", C_VIDEO, nullptr,
        {{"Ninguno (píxel nítido)", "nearest"}, {"Bilineal", "bilinear"}, {"Sharp Bilinear", "sharp"},
         {"Bicúbico Catmull-Rom", "bicubic"}, {"ScaleFX (pixel art 2D)", "scalefx"}, {"Scale2x", "scale2x"},
         {"Scale3x", "scale3x"}, {"Eagle 2x", "eagle2x"}}, 2,
        "Cómo se agranda la imagen hasta la pantalla. ScaleFX, Scale2x, Scale3x y Eagle 2x redondean los "
        "bordes de los píxeles usando solo los colores originales: son para juegos 2D (Castlevania SOTN, "
        "Mega Man X4...) con resolución interna 1x; a 4x u 8x no se aplican. ScaleFX es el más suave, "
        "Scale2x/3x el más fiel al píxel y Eagle 2x el más redondeado. Con Vulkan, Sharp y Bicúbico usan "
        "bilineal por ahora; en OpenGL los escaladores de píxel usan Ninguno.", false},
    {"screen_fx", "Efecto de pantalla", C_VIDEO, nullptr,
        {{"Ninguno", "none"}, {"Scanlines", "scanlines"}, {"CRT (rejilla de apertura)", "crt"}, {"LCD", "lcd"},
         {"Subpíxel LCD", "subpixel"}}, 0,
        "Imita la pantalla donde se jugaba. Scanlines: las líneas oscuras de una tele. CRT: además, las "
        "franjas de color rojo, verde y azul de un tubo Trinitron. LCD: la rejilla fina de una pantalla "
        "portátil. Subpíxel LCD: cada píxel dividido en sus tres colores. Siguen las líneas reales de la "
        "PlayStation aunque subas la resolución interna; se ven mejor en el dock o a 1080p.", false},

    {"depth", "Profundidad de color", C_VIDEO, "beetle_psx_hw_depth",
        {{"16 bpp (nativa)", "16bpp(native)"}, {"32 bpp (sin bandas)", "32bpp"}}, 1, "", false},
    {"dither", "Dithering", C_VIDEO, "beetle_psx_hw_dither_mode",
        {{"1x (nativo)", "1x(native)"}, {"A resolución interna", "internal resolution"}, {"Desactivado", "disabled"}}, 2,
        "Desactivarlo junto a 32 bpp elimina la trama de puntos típica de PSX.", false},
    {"widescreen_ar", "Aspecto en pantalla exclusiva", C_VIDEO, "beetle_psx_hw_widescreen_hack_aspect_ratio",
        {{"16:9", "16:9"}, {"16:10", "16:10"}, {"21:9", "21:9"}}, 0, "", false},
    {"crop_overscan", "Recortar overscan", C_VIDEO, "beetle_psx_hw_crop_overscan",
        {{"Desactivado", "disabled"}, {"Estático", "static"}, {"Inteligente", "smart"}}, 1, "", false},
    {"vsync", "VSync", C_VIDEO, nullptr, ONOFF, 0, "", false},
    {"display_120", "Pantalla 120 Hz", C_VIDEO, nullptr,
        {{"Desactivada (60 Hz)", "off"}, {"VRR a la frecuencia del juego", "vrr"},
         {"Negro intercalado (BFI, menos desenfoque)", "bfi"}, {"Frames intermedios (experimental)", "interp"}}, 0,
        "Aprovecha la pantalla de 120 Hz de la consola en modo portátil (en el dock con una TV de 60 Hz se usa "
        "lo normal). VRR: la pantalla va a la frecuencia exacta del juego, así los juegos PAL (50 Hz) se ven "
        "sin saltos. BFI: entre cada frame se muestra uno negro, lo que reduce mucho el desenfoque de movimiento "
        "a cambio de algo de brillo y parpadeo. Frames intermedios: muestra una mezcla del frame anterior y el "
        "siguiente entre cada frame (no es generación con vectores de movimiento: suaviza, pero puede dejar "
        "estelas y añade unos 8 ms de retardo). Requiere Vulkan y VSync activado. La emulación sigue a su "
        "velocidad original.", false},
    {"show_fps", "Mostrar FPS", C_VIDEO, nullptr, OFFON, 0, "", false},

    // ---------------------------------------------------------------- ANTIALIASING
    {"aa_post", "Antialiasing de pantalla", C_AA, nullptr,
        {{"Desactivado", "off"}, {"FXAA (rápido)", "fxaa"}, {"TAA temporal (estilo TXAA)", "taa"},
         {"FXAA + TAA (máximo suavizado)", "fxaa_taa"}}, 0,
        "FXAA suaviza los bordes de la imagen final casi sin coste. TAA mezcla cada frame con el anterior: "
        "elimina el parpadeo de bordes y el temblor de los polígonos (TXAA real es una técnica de NVIDIA; esto es "
        "su equivalente temporal). Combínalo con MSAA para el resultado más limpio. TAA requiere Vulkan.", false},
    {"msaa", "MSAA (bordes de polígonos)", C_AA, "beetle_psx_hw_msaa",
        {{"Desactivado", "1x"}, {"2x", "2x"}, {"4x (recomendado)", "4x"}, {"8x (muy exigente)", "8x"}}, 0,
        "Suaviza los bordes de los polígonos 3D en origen. Al activarlo el primer arranque tarda más (se "
        "compilan los shaders, que luego quedan guardados). 8x a resolución 4x u 8x puede bajar de 60 fps: la "
        "protección de rendimiento lo reducirá antes que la resolución.", false},

    // ---------------------------------------------------------------- 3D / PGXP
    {"pgxp_mode", "PGXP (precisión geométrica)", C_3D, "beetle_psx_hw_pgxp_mode",
        {{"Desactivado", "disabled"}, {"Solo memoria", "memory only"}, {"Memoria + CPU (máximo)", "memory + CPU"}}, 1,
        "Arregla los polígonos que tiemblan y se deforman (wobble). Es lo que hace que caras como la de "
        "Leon en Resident Evil 2 se vean sólidas y no 'derritiéndose'. 'Memoria + CPU' es más preciso y más lento.", true},
    {"pgxp_texture", "Corrección de perspectiva de texturas", C_3D, "beetle_psx_hw_pgxp_texture", ONOFF, 0,
        "Elimina el 'warping' de texturas en suelos y paredes. Requiere PGXP activo.", false},
    {"pgxp_vertex", "Caché de vértices PGXP", C_3D, "beetle_psx_hw_pgxp_vertex", OFFON, 0,
        "Mejora juegos que pierden precisión entre frames. Puede causar glitches en algunos títulos.", false},
    {"pgxp_nclip", "Culling preciso (NCLIP)", C_3D, "beetle_psx_hw_pgxp_nclip", ONOFF, 0,
        "Evita huecos y polígonos que desaparecen en modelos de personajes.", false},
    {"pgxp_2d_tol", "Tolerancia 2D de PGXP", C_3D, "beetle_psx_hw_pgxp_2d_tol",
        {{"Desactivada", "disabled"}, {"0 px", "0px"}, {"1 px", "1px"}, {"2 px", "2px"}, {"4 px", "4px"}}, 0,
        "Ajusta elementos 2D dibujados como polígonos (HUD, menús) cuando PGXP los desalinea.", false},
    {"line_render", "Hack de líneas a polígonos", C_3D, "beetle_psx_hw_line_render",
        {{"Por defecto", "default"}, {"Agresivo", "aggressive"}, {"Desactivado", "disabled"}}, 0,
        "Cierra huecos de 1 píxel entre polígonos al escalar.", false},

    // ---------------------------------------------------------------- TEXTURAS
    {"tex_filter", "Filtrado de texturas", C_TEX, "beetle_psx_hw_filter",
        {{"Nearest (original)", "nearest"}, {"SABR", "SABR"}, {"xBR", "xBR"}, {"Bilineal", "bilinear"},
         {"3-point (estilo N64)", "3-point"}, {"JINC2", "JINC2"}}, 0,
        "Suaviza las texturas de los modelos 3D. xBR/SABR redondean bordes pixelados.", false},
    {"filter_ex_sprite", "Excluir sprites del filtro", C_TEX, "beetle_psx_hw_filter_exclude_sprite",
        {{"No", "disabled"}, {"Opacos", "opaque"}, {"Todos", "all"}}, 2, "", false},
    {"filter_ex_2d", "Excluir polígonos 2D del filtro", C_TEX, "beetle_psx_hw_filter_exclude_2d_polygon",
        {{"No", "disabled"}, {"Opacos", "opaque"}, {"Todos", "all"}}, 2,
        "Mantiene nítidos HUD y fondos pre-renderizados (RE2, FF7) mientras se filtran los modelos 3D.", false},
    {"adaptive_smooth", "Suavizado adaptativo 2D", C_TEX, "beetle_psx_hw_adaptive_smoothing", ONOFF, 0, "", false},
    {"supersampling", "Supersampling a nativa", C_TEX, "beetle_psx_hw_super_sampling", OFFON, 0,
        "Renderiza alto y reduce a 240p: look original sin dientes de sierra.", false},
    {"mdec_yuv", "Suavizado de croma en FMV", C_TEX, "beetle_psx_hw_mdec_yuv", OFFON, 1,
        "Mejora los vídeos (MDEC) reduciendo bloques de color.", false},
    {"scaled_uv", "Offset UV escalado", C_TEX, "beetle_psx_hw_scaled_uv_offset", ONOFF, 0,
        "Reduce costuras entre texturas al usar resolución alta.", false},

    // ---------------------------------------------------------------- CPU
    {"cpu_freq", "Overclock de CPU", C_CPU, "beetle_psx_hw_cpu_freq_scale",
        {{"100% (nativo)", "100%"}, {"125%", "125%"}, {"150%", "150%"}, {"200%", "200%"}, {"300%", "300%"}}, 0,
        "Reduce bajones de FPS del juego original. Puede romper juegos sensibles al tiempo.", true},
    {"gte_oc", "Overclock de GTE", C_CPU, "beetle_psx_hw_gte_overclock", OFFON, 0,
        "Acelera el coprocesador geométrico: menos ralentización en escenas 3D cargadas.", false},
    {"gpu_oc", "Overclock de rasterizador GPU", C_CPU, "beetle_psx_hw_gpu_overclock",
        {{"1x (nativo)", "1x(native)"}, {"2x", "2x"}, {"4x", "4x"}}, 0, "", false},
    {"gpu_thread", "Hilo de GPU", C_CPU, "beetle_psx_hw_gpu_thread",
        {{"Activado (recomendado)", "enabled"}, {"Desactivado", "disabled"}}, 0,
        "Emula la GPU de la PlayStation en un hilo aparte, en otro núcleo de la CPU de la Switch 2. "
        "Desactívalo solo si un juego da problemas gráficos raros.", true},
    {"software_fb", "Framebuffer por software", C_CPU, "beetle_psx_hw_renderer_software_fb",
        {{"Activado (máxima compatibilidad)", "enabled"}, {"Desactivado (más rápido)", "disabled"}}, 0,
        "Necesario para algunos efectos (desenfoques, transiciones, capturas del propio juego). "
        "Desactivarlo ahorra CPU; si ves efectos rotos, vuelve a activarlo.", true},
    {"audio_sync", "Sincronización", C_CPU, nullptr,
        {{"Automática (VSync + audio dinámico)", "enabled"}, {"Sin sincronizar (pruebas)", "disabled"}}, 0,
        "Automática: la imagen va al ritmo de la pantalla (60 Hz) y el audio se ajusta solo un 0,5% para que no "
        "haya cortes ni tirones. Con VSync apagado, el audio marca la velocidad.", false},
    {"perf_guard", "Protección de rendimiento", C_CPU, nullptr,
        {{"Automática (baja la resolución si hace falta)", "auto"}, {"Solo avisar", "warn"}, {"Desactivada", "off"}}, 0,
        "Si el juego cae por debajo de 57 fps durante 3 segundos, baja un nivel la resolución interna de ese "
        "juego (se recuerda en sus ajustes). Evita que las mejoras saturen la consola.", false},

    // ---------------------------------------------------------------- CD / BIOS
    {"skip_bios", "Saltar logo de BIOS", C_CD, "beetle_psx_hw_skip_bios", OFFON, 0, "", true},
    {"cd_access", "Acceso al CD", C_CD, "beetle_psx_hw_cd_access_method",
        {{"Síncrono", "sync"}, {"Asíncrono", "async"}, {"Precargar en RAM", "precache"}}, 0,
        "Precargar elimina tirones de lectura en la microSD (usa más RAM).", true},
    {"cd_fastload", "Carga rápida de CD", C_CD, "beetle_psx_hw_cd_fastload",
        {{"2x (nativo)", "2x(native)"}, {"4x", "4x"}, {"8x", "8x"}}, 0,
        "Acorta pantallas de carga. Algunos juegos pueden fallar.", false},

    // ---------------------------------------------------------------- MEMCARDS
    {"memcard0_method", "Formato memcard ranura 1", C_MC, "beetle_psx_hw_use_mednafen_memcard0_method",
        {{"Libretro (.srm)", "libretro"}, {"Mednafen (.mcr)", "mednafen"}}, 0, "", true},
    {"memcard1", "Activar memcard ranura 2", C_MC, "beetle_psx_hw_enable_memcard1", ONOFF, 0, "", true},
    {"shared_mc", "Memcards compartidas entre juegos", C_MC, "beetle_psx_hw_shared_memory_cards", OFFON, 0,
        "Una sola tarjeta para todos los juegos, como una consola real.", true},
    {"autosave", "Guardado automático de memcard", C_MC, nullptr,
        {{"Cada 5 s si cambia", "5"}, {"Cada 30 s si cambia", "30"}, {"Solo al salir", "0"}}, 0, "", false},

    // ---------------------------------------------------------------- CONTROLES
    {"pad1_type", "Mando jugador 1", C_INPUT, nullptr,
        {{"Mando digital (todos los juegos)", "digital"}, {"DualShock (analógico)", "dualshock"}}, 0,
        "Digital funciona con todos los juegos y el stick izquierdo hace de cruceta. DualShock añade los dos "
        "sticks analógicos y vibración (Ape Escape, Spyro, Gran Turismo...), pero algunos juegos antiguos "
        "ignoran el mando en modo analógico.", true},
    {"pad2_type", "Mando jugador 2", C_INPUT, nullptr,
        {{"Mando digital", "digital"}, {"DualShock (analógico)", "dualshock"}, {"Ninguno", "none"}}, 0, "", true},
    {"stick_dpad", "Stick izquierdo como cruceta", C_INPUT, nullptr,
        {{"Automático (con mando Digital)", "auto"}, {"Siempre", "on"}, {"Nunca", "off"}}, 0,
        "Automático: con el mando Digital el stick izquierdo mueve como la cruceta; con DualShock el juego "
        "recibe el stick analógico de verdad. Siempre: también con DualShock (útil en juegos que solo usan la "
        "cruceta en los menús).", false},
    {"analog_toggle", "Botón ANALOG (L3+R3+Start)", C_INPUT, "beetle_psx_hw_analog_toggle", OFFON, 0, "", false},
    {"rumble", "Vibración", C_INPUT, nullptr, ONOFF, 0, "", false},
    {"face_layout", "Distribución de botones", C_INPUT, nullptr,
        {{"Por posición (B=X, A=O, Y=□, X=△)", "position"}, {"Nintendo (A=X, B=O)", "nintendo"}}, 0, "", false},

    // ---------------------------------------------------------------- BIBLIOTECA
    {"ui_background", "Fondo de la interfaz", "Biblioteca", nullptr,
        {{"Degradado animado", "animated"}, {"Imagen propia (fondo.png / fondo.jpg)", "image"}, {"Liso", "plain"}}, 0,
        "Lo que se ve detrás del menú cuando no hay juego cargado (con un juego, se ve el juego en pausa). "
        "Imagen propia: copia desde el PC una imagen llamada fondo.png o fondo.jpg en sdmc:/switch/psx-k2/ "
        "(mejor 1920x1080). Se oscurece un poco para que el texto se lea bien.", false},
    {"library_view", "Vista de la biblioteca", "Biblioteca", nullptr,
        {{"Cuadrícula con portadas", "grid"}, {"Lista", "list"}}, 0, "", false},
    {"covers_auto", "Descargar portadas automáticamente", "Biblioteca", nullptr, ONOFF, 0,
        "Con internet, descarga la portada de cada juego (por su nombre) y la guarda en la carpeta covers/. "
        "Para usar una tuya, copia una imagen .png o .jpg con el mismo nombre que el juego en esa carpeta.", false},
    {"covers_verify", "Verificar certificados (HTTPS)", "Biblioteca", nullptr, ONOFF, 0,
        "Recomendado. Desactívalo solo si las portadas no descargan aunque tengas internet.", false},

    // ---------------------------------------------------------------- SISTEMA
    {"cpu_boost", "Modo de rendimiento (boost)", C_SYS, nullptr, OFFON, 0,
        "Sube el reloj de CPU con appletSetCpuBoostMode mientras juegas.", false},
    {"state_slot", "Ranura de estado rápido", C_SYS, nullptr,
        {{"0","0"},{"1","1"},{"2","2"},{"3","3"},{"4","4"},{"5","5"},{"6","6"},{"7","7"},{"8","8"},{"9","9"}}, 0, "", false},
    };
    assignFlags(defs_);
}

static const struct { const char* id; unsigned flags; } kFlags[] = {
    {"internal_res", OPT_HW_ONLY}, {"msaa", OPT_VULKAN_ONLY}, {"depth", OPT_HW_ONLY}, {"dither", OPT_HW_ONLY},
    {"widescreen_ar", 0}, {"tex_filter", OPT_HW_ONLY}, {"filter_ex_sprite", OPT_HW_ONLY},
    {"filter_ex_2d", OPT_HW_ONLY}, {"adaptive_smooth", OPT_VULKAN_ONLY}, {"supersampling", OPT_HW_ONLY},
    {"mdec_yuv", OPT_VULKAN_ONLY}, {"display_120", OPT_VULKAN_ONLY}, {"screen_fx", 0}, {"scaled_uv", OPT_HW_ONLY}, {"pgxp_texture", OPT_HW_ONLY},
};

bool Config::appliesToRenderer(const OptionDef& d) const {
    bool vk = !strcmp(activeRenderer(), "hardware_vk");
    if ((d.flags & OPT_VULKAN_ONLY) && !vk) return false;
    if ((d.flags & OPT_HW_ONLY) && !strcmp(activeRenderer(), "software")) return false;
    return true;
}

// Perfiles: id -> índice de opción para cada perfil (Rendimiento, Equilibrado, Calidad)
static const struct { const char* id; int perf, bal, qual; } kPreset[] = {
    {"internal_res", 1, 2, 3}, {"msaa", 0, 0, 1}, {"depth", 1, 1, 1}, {"dither", 2, 2, 2},
    {"pgxp_mode", 1, 1, 2}, {"pgxp_texture", 0, 0, 0}, {"pgxp_nclip", 0, 0, 0},
    {"tex_filter", 0, 0, 2}, {"filter_ex_sprite", 2, 2, 2}, {"filter_ex_2d", 2, 2, 2},
    {"adaptive_smooth", 1, 0, 0}, {"supersampling", 0, 0, 0}, {"mdec_yuv", 0, 1, 1}, {"scaled_uv", 0, 0, 0},
};

void Config::applyPreset(int idx) {
    if (idx <= 0) return;
    applyingPreset_ = true;
    for (auto& p : kPreset) setIndex(p.id, idx == 1 ? p.perf : idx == 2 ? p.bal : p.qual);
    applyingPreset_ = false;
}

static void assignFlags(std::vector<OptionDef>& defs) {
    for (auto& d : defs) for (auto& f : kFlags) if (!strcmp(d.id, f.id)) d.flags = f.flags;
}

std::vector<std::string> Config::categories() const {
    std::vector<std::string> out;
    for (auto& d : defs_) {
        bool found = false;
        for (auto& c : out) if (c == d.category) found = true;
        if (!found) out.push_back(d.category);
    }
    return out;
}

const OptionDef* Config::find(const char* id) const {
    for (auto& d : defs_) if (!strcmp(d.id, id)) return &d;
    return nullptr;
}

int Config::index(const char* id) const {
    const OptionDef* d = find(id);
    if (!d) return 0;
    auto g = game_.find(id);
    if (hasGame() && g != game_.end()) return g->second;
    auto it = global_.find(id);
    return it != global_.end() ? it->second : d->defIndex;
}

const char* Config::value(const char* id) const {
    const OptionDef* d = find(id);
    return d ? d->choices[index(id)].value : "";
}

bool Config::isOverridden(const char* id) const { return hasGame() && game_.count(id); }

void Config::removeOverride(const char* id) {
    const OptionDef* d = find(id);
    if (!d || !game_.erase(id)) return;
    if (d->coreKey || !strcmp(id, "screen_mode")) coreDirty_ = true;
    if (!d->coreKey) feDirty_ = true;
}

void Config::setIndex(const char* id, int idx) {
    const OptionDef* d = find(id);
    if (!d || idx < 0 || idx >= (int)d->choices.size()) return;
    if (editScope == Scope::Game && hasGame()) game_[id] = idx; else global_[id] = idx;
    if (!strcmp(id, "gfx_preset")) applyPreset(idx);
    else if (!applyingPreset_) {
        // Tocar a mano una opción del perfil lo convierte en "Personalizado"
        for (auto& p : kPreset)
            if (!strcmp(p.id, id)) {
                if (editScope == Scope::Game && hasGame()) game_["gfx_preset"] = 0; else global_["gfx_preset"] = 0;
                break;
            }
    }
    if (d->coreKey || !strcmp(id, "screen_mode")) coreDirty_ = true;   // exclusiva activa el panorámico del núcleo
    if (!d->coreKey) feDirty_ = true;
}

const char* Config::coreValue(const char* coreKey) const {
    if (!strcmp(coreKey, "beetle_psx_hw_renderer")) return activeRenderer();
    // Con el renderizador por software la resolución interna (oculta en ese modo) va siempre a 1x:
    // a 4x la CPU no llega y los frames serían enormes
    if (!strcmp(coreKey, "beetle_psx_hw_internal_resolution") && !strcmp(activeRenderer(), "software")) return "1x(native)";
    if (!strcmp(coreKey, "beetle_psx_hw_widescreen_hack"))
        return !strcmp(value("screen_mode"), "exclusive") ? "enabled" : "disabled";
    for (auto& d : defs_)
        if (d.coreKey && !strcmp(d.coreKey, coreKey)) return d.choices[index(d.id)].value;
    return nullptr;
}

void Config::parseIni(const std::string& path, std::map<std::string, int>& out) {
    out.clear();
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        std::string key = line, val = eq + 1;
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '"')) s.pop_back();
            while (!s.empty() && (s.front() == ' ' || s.front() == '"')) s.erase(0, 1);
        };
        trim(key); trim(val);
        const OptionDef* d = find(key.c_str());
        if (!d) continue;
        for (size_t i = 0; i < d->choices.size(); i++)
            if (val == d->choices[i].value) out[key] = (int)i;
    }
    fclose(f);
}

void Config::writeIni(const std::string& path, const std::map<std::string, int>& m, bool full) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "# PSX-K2 configuración%s\n", full ? " global" : " por juego (solo overrides)");
    std::string cat;
    for (auto& d : defs_) {
        auto it = m.find(d.id);
        if (!full && it == m.end()) continue;
        if (cat != d.category) { cat = d.category; fprintf(f, "\n[%s]\n", cat.c_str()); }
        int idx = it != m.end() ? it->second : d.defIndex;
        fprintf(f, "%s = \"%s\"\n", d.id, d.choices[idx].value);
    }
    fclose(f);
}

void Config::resetCategory(const char* category) {
    std::map<std::string, int>& m = (editScope == Scope::Game && hasGame()) ? game_ : global_;
    for (auto& d : defs_) {
        if (category && strcmp(d.category, category)) continue;
        if (!strcmp(d.id, "state_slot")) continue;
        m.erase(d.id);
    }
    coreDirty_ = feDirty_ = true;
    if (&m == &global_) saveGlobal(); else saveGame();
}

void Config::loadGlobal() { parseIni(paths::config + "/psx-k2.ini", global_); coreDirty_ = feDirty_ = true; }
void Config::saveGlobal() { writeIni(paths::config + "/psx-k2.ini", global_, true); }

void Config::loadGame(const std::string& gameStem) {
    gameStem_ = gameStem;
    parseIni(paths::gameConfigs + "/" + gameStem + ".ini", game_);
    coreDirty_ = feDirty_ = true;
}
void Config::saveGame() {
    if (!hasGame()) return;
    std::string p = paths::gameConfigs + "/" + gameStem_ + ".ini";
    if (game_.empty()) remove(p.c_str()); else writeIni(p, game_, false);
}
void Config::clearGame() { game_.clear(); saveGame(); coreDirty_ = feDirty_ = true; }
void Config::unloadGame() { gameStem_.clear(); game_.clear(); editScope = Scope::Global; coreDirty_ = feDirty_ = true; }
