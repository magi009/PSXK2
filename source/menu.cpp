#include "menu.h"
#include "config.h"
#include "core_host.h"
#include "input.h"
#include "log.h"
#include "paths.h"
#include "video.h"
#include "imgui.h"
#include <switch.h>
#include <cstring>

static u64 s_lastTick = 0;

bool Menu::init() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.ConfigNavCursorVisibleAlways = true;

    // Fuente del sistema (incluye acentos y ñ)
    float px = 30.f * video_.screenH() / 1080.f + 6.f;
    PlFontData font;
    if (R_SUCCEEDED(plInitialize(PlServiceType_User)) &&
        R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) {
        ImFontConfig cfg; cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(font.address, font.size, px, &cfg, io.Fonts->GetGlyphRangesDefault());
    } else {
        ImFontConfig cfg; cfg.SizePixels = px; io.Fonts->AddFontDefault(&cfg);
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.ScaleAllSizes(video_.screenH() / 720.f);
    st.WindowRounding = 10; st.FrameRounding = 6; st.TabRounding = 6;
    st.Colors[ImGuiCol_Header]        = ImVec4(0.20f, 0.28f, 0.55f, 0.85f);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.38f, 0.75f, 0.95f);
    st.Colors[ImGuiCol_TabSelected]   = ImVec4(0.28f, 0.38f, 0.75f, 1.00f);
    return video_.imguiInit();
}

void Menu::shutdown() {
    video_.imguiShutdown();
    ImGui::DestroyContext();
    plExit();
}

void Menu::onGameLoaded(const std::string& rom) {
    error_.clear(); open = false; tab_ = 1;
    toast("Cargado: " + paths::stem(rom));
}

void Menu::toast(const std::string& msg) {
    toast_ = msg;
    toastUntil_ = armGetSystemTick() + armNsToTicks(2500000000ULL);
}

// ------------------------------------------------------------------ entrada del mando -> ImGui
void Menu::feedInput() {
    ImGuiIO& io = ImGui::GetIO();
    u64 now = armGetSystemTick();
    io.DeltaTime = s_lastTick ? (float)(armTicksToNs(now - s_lastTick) / 1e9) : 1.f / 60.f;
    if (io.DeltaTime <= 0) io.DeltaTime = 1.f / 60.f;
    s_lastTick = now;
    io.DisplaySize = ImVec2((float)video_.screenW(), (float)video_.screenH());

    u64 h = input_.held();
    auto key = [&](ImGuiKey k, u64 mask) { io.AddKeyEvent(k, (h & mask) != 0); };
    key(ImGuiKey_GamepadDpadUp, HidNpadButton_Up | HidNpadButton_StickLUp);
    key(ImGuiKey_GamepadDpadDown, HidNpadButton_Down | HidNpadButton_StickLDown);
    key(ImGuiKey_GamepadDpadLeft, HidNpadButton_Left | HidNpadButton_StickLLeft);
    key(ImGuiKey_GamepadDpadRight, HidNpadButton_Right | HidNpadButton_StickLRight);
    key(ImGuiKey_GamepadFaceDown, HidNpadButton_A);    // confirmar (convención Nintendo)
    key(ImGuiKey_GamepadFaceRight, HidNpadButton_B);   // volver
    key(ImGuiKey_GamepadFaceUp, HidNpadButton_X);

    HidTouchScreenState ts = {};
    if (hidGetTouchScreenStates(&ts, 1) && ts.count > 0) {
        io.AddMousePosEvent(ts.touches[0].x * io.DisplaySize.x / 1280.f, ts.touches[0].y * io.DisplaySize.y / 720.f);
        io.AddMouseButtonEvent(0, true);
    } else {
        io.AddMouseButtonEvent(0, false);
    }

    u64 d = input_.down();
    if (d & HidNpadButton_R) wantTab_ = (tab_ + 1) % 4;
    if (d & HidNpadButton_L) wantTab_ = (tab_ + 3) % 4;
}

// ------------------------------------------------------------------ pestañas
void Menu::drawLibrary() {
    if (!scanned_) { roms_ = paths::scanRoms(); scanned_ = true; }
    auto missing = paths::missingBios();
    if (!missing.empty())
        ImGui::TextColored(ImVec4(1, .45f, .35f, 1), "BIOS no encontrada: copia scph5501.bin / scph5500.bin / scph5502.bin en bios/");
    if (!error_.empty()) ImGui::TextColored(ImVec4(1, .45f, .35f, 1), "%s", error_.c_str());
    if (ImGui::Button("Volver a escanear")) scanned_ = false;
    ImGui::SameLine(); ImGui::TextDisabled("%zu juegos en %s", roms_.size(), paths::roms.c_str());
    ImGui::Separator();
    ImGui::BeginChild("roms");
    if (roms_.empty()) ImGui::TextWrapped("Copia tus juegos (.cue/.bin, .chd, .pbp, .m3u) en la carpeta roms/.");
    for (auto& r : roms_) {
        std::string label = paths::stem(r) + "   " + paths::ext(r);
        if (ImGui::Selectable(label.c_str())) pendingRom = r;
    }
    ImGui::EndChild();
}

void Menu::drawGame() {
    if (!core_.loaded()) { ImGui::TextDisabled("No hay ningún juego cargado."); return; }
    Config& c = Config::get();
    int slot = c.index("state_slot");
    ImGui::Text("%s", core_.gameStem().c_str());
    ImGui::Separator();
    ImVec2 bw(ImGui::GetContentRegionAvail().x, 0);
    if (ImGui::Button("Continuar", bw)) open = false;
    if (ImGui::Button("Reiniciar", bw)) { core_.reset(); open = false; }
    char buf[96];
    snprintf(buf, sizeof buf, "Guardar estado (ranura %d)", slot);
    if (ImGui::Button(buf, bw)) toast(core_.saveState(slot) ? "Estado guardado" : "Error al guardar estado");
    snprintf(buf, sizeof buf, "Cargar estado (ranura %d)%s", slot, core_.stateExists(slot) ? "" : "  [vacía]");
    if (ImGui::Button(buf, bw)) { if (core_.loadState(slot)) { open = false; toast("Estado cargado"); } else toast("Ranura vacía"); }
    if (ImGui::Button("Ranura anterior", ImVec2(bw.x / 2 - 4, 0))) c.setIndex("state_slot", (slot + 9) % 10);
    ImGui::SameLine();
    if (ImGui::Button("Ranura siguiente", ImVec2(bw.x / 2 - 4, 0))) c.setIndex("state_slot", (slot + 1) % 10);
    if (core_.discCount() > 1) {
        snprintf(buf, sizeof buf, "Cambiar disco (actual %u de %u)", core_.discIndex() + 1, core_.discCount());
        if (ImGui::Button(buf, bw)) {
            unsigned n = (core_.discIndex() + 1) % core_.discCount();
            toast(core_.swapDisc(n) ? "Disco " + std::to_string(n + 1) + " insertado" : "No se pudo cambiar el disco");
        }
    }
    if (ImGui::Button("Captura de pantalla", bw)) {
        open = false;           // se toma en el siguiente frame sin menú (ver main)
        screenshotRequested = true;
    }
    if (ImGui::Button("Guardar memory card ahora", bw)) { core_.flushMemcard(true); toast("Memory card guardada"); }
    if (ImGui::Button("Cerrar juego", bw)) { pendingUnload = true; tab_ = 0; wantTab_ = 0; }
}

void Menu::drawSettings() {
    Config& c = Config::get();
    if (core_.loaded()) {
        int s = c.editScope == Scope::Game ? 1 : 0;
        ImGui::Text("Editando:"); ImGui::SameLine();
        if (ImGui::RadioButton("Global", s == 0)) c.editScope = Scope::Global;
        ImGui::SameLine();
        if (ImGui::RadioButton("Solo este juego", s == 1)) c.editScope = Scope::Game;
        ImGui::SameLine();
        if (ImGui::SmallButton("Borrar ajustes del juego")) c.clearGame();
    }
    static int cat = 0;
    auto cats = c.categories();
    u64 d = input_.down();
    if (d & HidNpadButton_ZR) cat = (cat + 1) % cats.size();
    if (d & HidNpadButton_ZL) cat = (cat + (int)cats.size() - 1) % cats.size();
    ImGui::TextDisabled("ZL/ZR: categoría   Izq/Der o A: cambiar valor   ★ = ajuste propio del juego");
    if (ImGui::BeginTabBar("cats")) {
        for (int i = 0; i < (int)cats.size(); i++) {
            ImGuiTabItemFlags f = 0;
            bool sel = false;
            if (i == cat) f |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem(cats[i].c_str(), nullptr, f)) { sel = true; ImGui::EndTabItem(); }
            if (sel && ImGui::IsItemClicked()) cat = i;
        }
        ImGui::EndTabBar();
    }
    float helpH = ImGui::GetTextLineHeightWithSpacing() * 3.2f;
    ImGui::BeginChild("opts", ImVec2(0, -helpH));
    help_ = "";
    for (auto& o : c.defs()) {
        if (cats[cat] != o.category) continue;
        int idx = c.index(o.id);
        std::string label = std::string(c.isOverridden(o.id) ? "★ " : "") + o.label + (o.needsRestart ? "  (reinicia juego)" : "");
        ImGui::PushID(o.id);
        bool act = ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowOverlap);
        bool foc = ImGui::IsItemFocused() || ImGui::IsItemHovered();
        const char* val = o.choices[idx].label;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize(val).x - 40);
        ImGui::TextColored(ImVec4(.55f, .75f, 1, 1), "< %s >", val);
        int n = (int)o.choices.size();
        if (foc) {
            help_ = o.help;
            if (d & HidNpadButton_Right) c.setIndex(o.id, (idx + 1) % n);
            if (d & HidNpadButton_Left) c.setIndex(o.id, (idx + n - 1) % n);
        }
        if (act) c.setIndex(o.id, (idx + 1) % n);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextWrapped("%s", help_);
}

void Menu::drawInfo() {
    ImGui::Text("PSX-K2 0.2.0  —  Beetle PSX HW (GPLv2) · libounce + OunceVK (%s)", video_.isVulkan() ? "Vulkan" : "OpenGL");
    ImGui::Separator();
    ImGui::Text("Atajos en juego (mantén  −  y pulsa):");
    ImGui::BulletText("+   abrir / cerrar este menú");
    ImGui::BulletText("R   guardar estado      L   cargar estado");
    ImGui::BulletText("ZR  avance rápido (mantener)");
    ImGui::BulletText("Y   captura de pantalla     X   siguiente disco");
    ImGui::BulletText("+ mantenido 2 s   salir al hbmenu (emergencia)");
    ImGui::Separator();
    ImGui::Text("Carpetas:");
    ImGui::BulletText("BIOS: %s", paths::bios.c_str());
    ImGui::BulletText("Juegos: %s", paths::roms.c_str());
    ImGui::BulletText("Memory cards: %s", paths::memcards.c_str());
    ImGui::BulletText("Estados: %s", paths::states.c_str());
    ImGui::BulletText("Capturas: %s", paths::screenshots.c_str());
    ImGui::BulletText("Config: %s", paths::config.c_str());
    ImGui::Separator();
    ImGui::Text("Salida actual: %dx%d", video_.screenW(), video_.screenH());
    if (ImGui::Button("Salir del emulador")) quit = true;
}

// ------------------------------------------------------------------ frame
void Menu::draw() {
    feedInput();
    video_.imguiNewFrame();
    ImGui::NewFrame();
    video_.addGameLayer();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 sz(io.DisplaySize.x * 0.86f, io.DisplaySize.y * 0.86f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.07f, io.DisplaySize.y * 0.07f));
    ImGui::SetNextWindowSize(sz);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("PSX-K2", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    static const char* kTabs[4] = {"Biblioteca", "Juego", "Configuración", "Información"};
    if (ImGui::BeginTabBar("main")) {
        for (int i = 0; i < 4; i++) {
            ImGuiTabItemFlags f = (wantTab_ == i) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(kTabs[i], nullptr, f)) {
                tab_ = i;
                if (i == 0) drawLibrary(); else if (i == 1) drawGame(); else if (i == 2) drawSettings(); else drawInfo();
                ImGui::EndTabItem();
            }
        }
        wantTab_ = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
    if (!toast_.empty() && armGetSystemTick() < toastUntil_) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.96f), 0, ImVec2(0.5f, 1));
        ImGui::Begin("toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(toast_.c_str());
        ImGui::End();
    }
    ImGui::Render();
    video_.imguiRender(ImGui::GetDrawData());
    if (input_.down() & HidNpadButton_B && core_.loaded() && tab_ != 2) open = false;
}

void Menu::drawOsd() {
    bool fps = Config::get().isOn("show_fps");
    bool t = !toast_.empty() && armGetSystemTick() < toastUntil_;
    if (!fps && !t && !video_.isVulkan()) return;   // en Vulkan el juego se dibuja con este mismo pase
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)video_.screenW(), (float)video_.screenH());
    io.DeltaTime = 1.f / 60.f;
    video_.imguiNewFrame();
    ImGui::NewFrame();
    video_.addGameLayer();
    if (fps || t) {
    ImGui::SetNextWindowPos(ImVec2(12, 12));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("osd", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
    if (fps) ImGui::Text("%.1f FPS", core_.fps());
    if (t) ImGui::TextUnformatted(toast_.c_str());
    ImGui::End();
    }
    ImGui::Render();
    video_.imguiRender(ImGui::GetDrawData());
}
