#include "menu.h"
#include "config.h"
#include "core_host.h"
#include "input.h"
#include "log.h"
#include "paths.h"
#include "video.h"
#include "imgui.h"
#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

// Iconos de botones de la fuente NintendoExt del sistema
#define G_A  ""
#define G_B  ""
#define G_X  ""
#define G_Y  ""
#define G_L  ""
#define G_R  ""
#define G_ZL ""
#define G_ZR ""
#define G_PLUS  ""
#define G_MINUS ""

// Paleta
static const ImVec4 C_BG      = ImVec4(0.055f, 0.067f, 0.094f, 0.96f);
static const ImVec4 C_PANEL   = ImVec4(0.090f, 0.106f, 0.149f, 1.00f);
static const ImVec4 C_PANEL2  = ImVec4(0.125f, 0.145f, 0.200f, 1.00f);
static const ImVec4 C_ACCENT  = ImVec4(0.424f, 0.361f, 0.906f, 1.00f);   // violeta
static const ImVec4 C_ACCENT2 = ImVec4(0.000f, 0.706f, 0.847f, 1.00f);   // cian
static const ImVec4 C_TEXT    = ImVec4(0.902f, 0.914f, 0.941f, 1.00f);
static const ImVec4 C_MUTED   = ImVec4(0.541f, 0.576f, 0.651f, 1.00f);
static const ImVec4 C_DANGER  = ImVec4(0.922f, 0.341f, 0.341f, 1.00f);
static const ImVec4 C_WARN    = ImVec4(1.000f, 0.690f, 0.290f, 1.00f);

static u64 s_lastTick = 0;
static float easeOut(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
static ImU32 col(ImVec4 c, float a = 1.f) { c.w *= a; return ImGui::GetColorU32(c); }

// ------------------------------------------------------------------ init
bool Menu::init() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.ConfigNavCursorVisibleAlways = true;
    io.KeyRepeatDelay = 0.28f;      // desplazamiento fluido al mantener la cruceta
    io.KeyRepeatRate = 0.065f;

    scale_ = video_.screenH() / 1080.f;
    float body = std::round(30.f * scale_) + 4.f, title = std::round(body * 1.55f);
    static const ImWchar extRange[] = {0xE000, 0xE152, 0};
    // Latín + símbolos que usa la interfaz (◀ ▶ ★)
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        b.AddText("\u25C0\u25B6\u2605\u2026");
        b.BuildRanges(&ranges);
    }
    PlFontData std_, ext;
    bool havePl = R_SUCCEEDED(plInitialize(PlServiceType_User)) &&
                  R_SUCCEEDED(plGetSharedFontByType(&std_, PlSharedFontType_Standard));
    auto addFont = [&](float px) -> ImFont* {
        ImFontConfig cfg; cfg.FontDataOwnedByAtlas = false; cfg.OversampleH = 2;
        ImFont* f = havePl ? io.Fonts->AddFontFromMemoryTTF(std_.address, std_.size, px, &cfg, ranges.Data)
                           : io.Fonts->AddFontDefault();
        // Iconos de botones (A, B, L, R...) mezclados en la misma fuente
        if (havePl && R_SUCCEEDED(plGetSharedFontByType(&ext, PlSharedFontType_NintendoExt))) {
            ImFontConfig e; e.FontDataOwnedByAtlas = false; e.MergeMode = true; e.GlyphOffset.y = px * 0.08f;
            io.Fonts->AddFontFromMemoryTTF(ext.address, ext.size, px, &e, extRange);
        }
        return f;
    };
    fontBody_ = addFont(body);
    fontTitle_ = addFont(title);
    psmInitialize();
    applyStyle();
    return video_.imguiInit();
}

void Menu::applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(18, 12);
    st.ItemSpacing = ImVec2(14, 10);
    st.ItemInnerSpacing = ImVec2(10, 8);
    st.ScrollbarSize = 10;
    st.WindowRounding = 18; st.ChildRounding = 14; st.FrameRounding = 12; st.PopupRounding = 16;
    st.GrabRounding = 10; st.ScrollbarRounding = 10;
    st.WindowBorderSize = 0; st.ChildBorderSize = 0; st.PopupBorderSize = 0; st.FrameBorderSize = 0;
    st.SelectableTextAlign = ImVec2(0, 0.5f);
    st.ScaleAllSizes(scale_);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = C_TEXT;                 c[ImGuiCol_TextDisabled] = C_MUTED;
    c[ImGuiCol_WindowBg] = C_BG;               c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = C_PANEL2;            c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_FrameBg] = C_PANEL2;            c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.20f, 0.28f, 1);
    c[ImGuiCol_Button] = C_PANEL2;             c[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.22f, 0.32f, 1);
    c[ImGuiCol_ButtonActive] = C_ACCENT;
    c[ImGuiCol_Header] = ImVec4(C_ACCENT.x, C_ACCENT.y, C_ACCENT.z, 0.30f);
    c[ImGuiCol_HeaderHovered] = ImVec4(C_ACCENT.x, C_ACCENT.y, C_ACCENT.z, 0.40f);
    c[ImGuiCol_HeaderActive] = ImVec4(C_ACCENT.x, C_ACCENT.y, C_ACCENT.z, 0.60f);
    c[ImGuiCol_NavCursor] = C_ACCENT2;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.15f);
    c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_CheckMark] = C_ACCENT2;
}

void Menu::shutdown() {
    video_.imguiShutdown();
    ImGui::DestroyContext();
    psmExit();
    plExit();
}

void Menu::onGameLoaded(const std::string& rom) {
    error_.clear(); open = false; tab_ = 1;
    toast("Cargado: " + paths::stem(rom));
}

void Menu::toast(const std::string& msg) {
    toast_ = msg;
    toastStart_ = armGetSystemTick();
    toastUntil_ = toastStart_ + armNsToTicks(2800000000ULL);
}

// ------------------------------------------------------------------ entrada del mando -> ImGui
void Menu::feedInput() {
    ImGuiIO& io = ImGui::GetIO();
    u64 now = armGetSystemTick();
    io.DeltaTime = s_lastTick ? (float)(armTicksToNs(now - s_lastTick) / 1e9) : 1.f / 60.f;
    if (io.DeltaTime <= 0 || io.DeltaTime > 0.25f) io.DeltaTime = 1.f / 60.f;
    s_lastTick = now;
    io.DisplaySize = ImVec2((float)video_.screenW(), (float)video_.screenH());

    u64 h = input_.held();
    auto key = [&](ImGuiKey k, u64 mask) { io.AddKeyEvent(k, (h & mask) != 0); };
    key(ImGuiKey_GamepadDpadUp, HidNpadButton_Up | HidNpadButton_StickLUp);
    key(ImGuiKey_GamepadDpadDown, HidNpadButton_Down | HidNpadButton_StickLDown);
    key(ImGuiKey_GamepadDpadLeft, HidNpadButton_Left | HidNpadButton_StickLLeft);
    key(ImGuiKey_GamepadDpadRight, HidNpadButton_Right | HidNpadButton_StickLRight);
    key(ImGuiKey_GamepadFaceDown, HidNpadButton_A);    // aceptar (convención Nintendo)
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
    if (!confirmOpen_) {
        if (d & HidNpadButton_R) wantTab_ = (tab_ + 1) % 4;
        if (d & HidNpadButton_L) wantTab_ = (tab_ + 3) % 4;
    }
}

// ------------------------------------------------------------------ piezas comunes
// Botón grande con color propio
static bool BigButton(const char* label, ImVec4 color, float w, float h) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 1.15f, color.y * 1.15f, color.z * 1.15f, color.w));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x * 0.85f, color.y * 0.85f, color.z * 0.85f, color.w));
    bool r = ImGui::Button(label, ImVec2(w, h));
    ImGui::PopStyleColor(3);
    return r;
}

// Tarjeta (fondo redondeado) alrededor de un bloque
static void BeginCard(const char* id, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 18));
    ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding);
}
static void EndCard() { ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(); }

void Menu::drawHeader() {
    ImGui::PushFont(fontTitle_);
    ImGui::TextColored(C_ACCENT, "PSX");
    ImGui::SameLine(0, 4);
    ImGui::TextColored(C_TEXT, "-K2");
    ImGui::PopFont();
    if (core_.loaded()) {
        ImGui::SameLine(0, 30);
        float y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y + ImGui::GetFontSize() * 0.45f);
        ImGui::TextColored(C_MUTED, "%s", core_.gameStem().c_str());
        ImGui::SetCursorPosY(y);
    }
    // Hora y batería a la derecha
    time_t t = time(nullptr);
    char clock[16]; strftime(clock, sizeof clock, "%H:%M", localtime(&t));
    u32 bat = 0; psmGetBatteryChargePercentage(&bat);
    char right[48]; snprintf(right, sizeof right, "%s     %u%%", clock, bat);
    float w = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.45f);
    ImGui::TextColored(C_TEXT, "%s", right);
}

void Menu::drawSidebar(float w) {
    static const char* kNames[4] = {"Biblioteca", "Juego", "Ajustes", "Información"};
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 14));
    // Sin navegación por mando: las secciones se cambian con L/R (o tocando)
    ImGui::BeginChild("side", ImVec2(w, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoNavInputs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float itemH = ImGui::GetFontSize() * 2.3f;
    for (int i = 0; i < 4; i++) {
        bool sel = tab_ == i;
        bool disabled = (i == 1 && !core_.loaded());
        ImVec2 p = ImGui::GetCursorScreenPos();
        float iw = ImGui::GetContentRegionAvail().x;
        if (sel) {
            dl->AddRectFilled(p, ImVec2(p.x + iw, p.y + itemH), col(C_ACCENT, 0.28f), 12 * scale_);
            dl->AddRectFilled(p, ImVec2(p.x + 6 * scale_, p.y + itemH), col(C_ACCENT), 3 * scale_);
        }
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("tab", ImVec2(iw, itemH)) && !disabled) wantTab_ = i;
        ImGui::PopID();
        ImVec2 ts = ImGui::CalcTextSize(kNames[i]);
        dl->AddText(ImVec2(p.x + 22 * scale_, p.y + (itemH - ts.y) * 0.5f),
                    col(disabled ? C_MUTED : sel ? C_TEXT : ImVec4(0.75f, 0.78f, 0.85f, 1), disabled ? 0.5f : 1.f), kNames[i]);
        ImGui::Dummy(ImVec2(0, 4 * scale_));
    }
    // Pie de la barra lateral
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFontSize() * 3.2f);
    ImGui::TextColored(C_MUTED, "%s  " G_L " " G_R, "Secciones");
    ImGui::TextColored(C_MUTED, "%s", video_.isVulkan() ? "Vulkan 1.4" : "OpenGL");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void Menu::drawFooter() {
    std::string hints = G_A " Aceptar     " G_B " " + std::string(core_.loaded() ? "Volver al juego" : "Volver");
    if (tab_ == 2) hints += "     " G_ZL " " G_ZR " Categoría     ◀ ▶ Cambiar";
    hints += "     " G_L " " G_R " Secciones";
    if (core_.loaded()) hints += "     " G_MINUS " + " G_PLUS " Menú";
    ImGui::TextColored(C_MUTED, "%s", hints.c_str());
}

void Menu::drawToast(float bottomY) {
    u64 now = armGetSystemTick();
    if (toast_.empty() || now >= toastUntil_) return;
    float since = (float)(armTicksToNs(now - toastStart_) / 1e9);
    float left = (float)(armTicksToNs(toastUntil_ - now) / 1e9);
    float a = std::min(easeOut(since / 0.2f), std::min(1.f, left / 0.4f));   // entra en 0,2 s, sale en 0,4 s
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 ts = ImGui::CalcTextSize(toast_.c_str());
    ImGuiIO& io = ImGui::GetIO();
    float padX = 28 * scale_, padY = 14 * scale_;
    float y = bottomY - (1.f - easeOut(since / 0.2f)) * 20 * scale_;
    ImVec2 p0(io.DisplaySize.x * 0.5f - ts.x * 0.5f - padX, y - ts.y - padY * 2);
    ImVec2 p1(io.DisplaySize.x * 0.5f + ts.x * 0.5f + padX, y);
    dl->AddRectFilled(p0, p1, col(ImVec4(0.10f, 0.11f, 0.16f, 0.94f), a), 40 * scale_);
    dl->AddRect(p0, p1, col(C_ACCENT, 0.6f * a), 40 * scale_, 0, 2 * scale_);
    dl->AddText(ImVec2(p0.x + padX, p0.y + padY), col(C_TEXT, a), toast_.c_str());
}

// ------------------------------------------------------------------ secciones
void Menu::drawLibrary() {
    if (!scanned_) { roms_ = paths::scanRoms(); scanned_ = true; }
    ImGui::PushFont(fontTitle_); ImGui::Text("Biblioteca"); ImGui::PopFont();
    ImGui::TextColored(C_MUTED, "%zu juegos en %s", roms_.size(), paths::roms.c_str());
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Volver a escanear").x - 40 * scale_);
    if (ImGui::Button("Volver a escanear")) scanned_ = false;

    if (!paths::missingBios().empty()) {
        ImGui::TextColored(C_WARN, "Falta la BIOS: copia scph5501.bin (USA), scph5500.bin (JAP) o scph5502.bin (EUR) en bios/");
    }
    if (!error_.empty()) ImGui::TextColored(C_DANGER, "%s", error_.c_str());
    ImGui::Dummy(ImVec2(0, 4 * scale_));

    ImGui::BeginChild("roms");
    if (roms_.empty()) {
        ImGui::TextWrapped("No hay juegos todavía. Copia tus juegos (.chd, .cue/.bin, .pbp, .m3u) en la carpeta roms/ "
                           "de la microSD y pulsa \"Volver a escanear\".");
    }
    float rowH = ImGui::GetFontSize() * 2.6f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < roms_.size(); i++) {
        const std::string& r = roms_[i];
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##rom", false, 0, ImVec2(w, rowH))) pendingRom = r;
        bool focused = ImGui::IsItemFocused() || ImGui::IsItemHovered();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), col(focused ? C_PANEL2 : C_PANEL), 12 * scale_);
        // Insignia con el formato
        std::string ext = paths::ext(r).substr(1);
        for (auto& ch : ext) ch = (char)toupper(ch);
        ImVec2 es = ImGui::CalcTextSize(ext.c_str());
        float bx = p.x + 18 * scale_, by = p.y + (rowH - es.y) * 0.5f;
        dl->AddRectFilled(ImVec2(bx - 8 * scale_, by - 4 * scale_), ImVec2(bx + es.x + 8 * scale_, by + es.y + 4 * scale_),
                          col(focused ? C_ACCENT : ImVec4(0.25f, 0.27f, 0.36f, 1)), 8 * scale_);
        dl->AddText(ImVec2(bx, by), col(C_TEXT), ext.c_str());
        std::string name = paths::stem(r);
        dl->AddText(ImVec2(bx + es.x + 30 * scale_, p.y + (rowH - ImGui::GetFontSize()) * 0.5f), col(C_TEXT), name.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void Menu::drawGame() {
    ImGui::PushFont(fontTitle_); ImGui::Text("Juego"); ImGui::PopFont();
    if (!core_.loaded()) { ImGui::TextColored(C_MUTED, "No hay ningún juego cargado. Elige uno en la Biblioteca."); return; }
    ImGui::TextColored(C_MUTED, "%s", core_.gameStem().c_str());
    ImGui::Dummy(ImVec2(0, 6 * scale_));

    Config& c = Config::get();
    int slot = c.index("state_slot");
    float W = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
    float half = (W - gap) * 0.5f, bh = ImGui::GetFontSize() * 2.4f;
    char buf[96];

    if (BigButton("Continuar", C_ACCENT, W, bh * 1.15f)) open = false;
    if (BigButton("Reiniciar", C_PANEL2, half, bh)) { core_.reset(); open = false; }
    ImGui::SameLine();
    if (BigButton("Captura de pantalla", C_PANEL2, half, bh)) { open = false; screenshotRequested = true; }

    snprintf(buf, sizeof buf, "Guardar estado  ·  ranura %d", slot);
    if (BigButton(buf, C_PANEL2, half, bh)) toast(core_.saveState(slot) ? "Estado guardado en la ranura " + std::to_string(slot) : "Error al guardar el estado");
    ImGui::SameLine();
    snprintf(buf, sizeof buf, "Cargar estado  ·  ranura %d%s", slot, core_.stateExists(slot) ? "" : " (vacía)");
    if (BigButton(buf, C_PANEL2, half, bh)) {
        if (core_.loadState(slot)) { open = false; toast("Estado cargado"); } else toast("La ranura " + std::to_string(slot) + " está vacía");
    }
    if (BigButton("◀  Ranura anterior", C_PANEL, half, bh)) c.setIndex("state_slot", (slot + 9) % 10);
    ImGui::SameLine();
    if (BigButton("Ranura siguiente  ▶", C_PANEL, half, bh)) c.setIndex("state_slot", (slot + 1) % 10);

    if (core_.discCount() > 1) {
        snprintf(buf, sizeof buf, "Cambiar disco  ·  disco %u de %u", core_.discIndex() + 1, core_.discCount());
        if (BigButton(buf, C_PANEL2, W, bh)) {
            unsigned n = (core_.discIndex() + 1) % core_.discCount();
            toast(core_.swapDisc(n) ? "Disco " + std::to_string(n + 1) + " insertado" : "No se pudo cambiar el disco");
        }
    }
    if (BigButton("Guardar memory card ahora", C_PANEL2, half, bh)) { core_.flushMemcard(true); toast("Memory card guardada"); }
    ImGui::SameLine();
    if (BigButton("Cerrar juego", ImVec4(C_DANGER.x * 0.7f, C_DANGER.y * 0.7f, C_DANGER.z * 0.7f, 1), half, bh)) {
        pendingUnload = true; wantTab_ = 0;
    }
}

void Menu::drawSettings() {
    Config& c = Config::get();
    u64 d = input_.down();
    auto cats = c.categories();
    if (cat_ >= (int)cats.size()) cat_ = 0;
    if (!confirmOpen_) {
        if (d & HidNpadButton_ZR) cat_ = (cat_ + 1) % cats.size();
        if (d & HidNpadButton_ZL) cat_ = (cat_ + (int)cats.size() - 1) % cats.size();
    }

    ImGui::PushFont(fontTitle_); ImGui::Text("Ajustes"); ImGui::PopFont();

    // Ámbito + Restaurar todo
    if (core_.loaded()) {
        bool game = c.editScope == Scope::Game;
        ImGui::TextColored(C_MUTED, "Editando:"); ImGui::SameLine();
        if (BigButton("Global", game ? C_PANEL2 : C_ACCENT, 0, 0)) c.editScope = Scope::Global;
        ImGui::SameLine();
        if (BigButton("Solo este juego", game ? C_ACCENT : C_PANEL2, 0, 0)) c.editScope = Scope::Game;
        ImGui::SameLine();
    }
    const char* allLabel = (core_.loaded() && c.editScope == Scope::Game) ? "Restaurar todo (este juego)" : "Restaurar todos los ajustes";
    if (BigButton(allLabel, ImVec4(0.35f, 0.17f, 0.20f, 1), 0, 0)) { confirmOpen_ = true; confirmCat_.clear(); }

    // Fichas de categoría
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    for (int i = 0; i < (int)cats.size(); i++) {
        if (i) ImGui::SameLine(0, 8 * scale_);
        if (ImGui::GetCursorPosX() + ImGui::CalcTextSize(cats[i].c_str()).x + 40 * scale_ > ImGui::GetContentRegionMax().x)
            ImGui::NewLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14 * scale_, 8 * scale_));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 30 * scale_);
        if (BigButton(cats[i].c_str(), i == cat_ ? C_ACCENT : C_PANEL, 0, 0)) cat_ = i;
        ImGui::PopStyleVar(2);
    }

    // Restaurar esta categoría
    std::string catReset = "Restaurar " + cats[cat_];
    if (ImGui::Button(catReset.c_str())) { confirmOpen_ = true; confirmCat_ = cats[cat_]; }

    // Lista de opciones
    float helpH = ImGui::GetFontSize() * 3.6f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::BeginChild("opts", ImVec2(0, -helpH), ImGuiChildFlags_AlwaysUseWindowPadding);
    help_ = "";
    float rowH = ImGui::GetFontSize() * 2.1f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (auto& o : c.defs()) {
        if (cats[cat_] != o.category) continue;
        int idx = c.index(o.id), n = (int)o.choices.size();
        ImGui::PushID(o.id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        bool act = ImGui::Selectable("##opt", false, 0, ImVec2(w, rowH));
        bool foc = ImGui::IsItemFocused() || ImGui::IsItemHovered();
        if (foc) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), col(C_PANEL2), 10 * scale_);
        float ty = p.y + (rowH - ImGui::GetFontSize()) * 0.5f;
        std::string label = std::string(c.isOverridden(o.id) ? "★ " : "") + o.label;
        dl->AddText(ImVec2(p.x + 16 * scale_, ty), col(C_TEXT), label.c_str());
        if (o.needsRestart) {
            float lx = p.x + 16 * scale_ + ImGui::CalcTextSize(label.c_str()).x + 12 * scale_;
            dl->AddText(ImVec2(lx, ty), col(C_MUTED), "· reinicia el juego");
        }
        // Valor en una "píldora" a la derecha
        std::string val = std::string("◀  ") + o.choices[idx].label + "  ▶";
        ImVec2 vs = ImGui::CalcTextSize(val.c_str());
        ImVec2 v1(p.x + w - 12 * scale_, p.y + rowH * 0.5f + vs.y * 0.5f + 6 * scale_);
        ImVec2 v0(v1.x - vs.x - 28 * scale_, p.y + rowH * 0.5f - vs.y * 0.5f - 6 * scale_);
        dl->AddRectFilled(v0, v1, col(foc ? C_ACCENT : ImVec4(0.20f, 0.22f, 0.30f, 1), foc ? 0.9f : 1.f), 30 * scale_);
        dl->AddText(ImVec2(v0.x + 14 * scale_, v0.y + 6 * scale_), col(C_TEXT), val.c_str());
        if (foc && !confirmOpen_) {
            help_ = o.help;
            if (d & HidNpadButton_Right) c.setIndex(o.id, (idx + 1) % n);
            if (d & HidNpadButton_Left) c.setIndex(o.id, (idx + n - 1) % n);
        }
        if (act) c.setIndex(o.id, (idx + 1) % n);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_MUTED, "%s", *help_ ? help_ : "★ = ajuste propio de este juego.  Los cambios se guardan solos.");
    ImGui::PopTextWrapPos();
}

void Menu::drawConfirm() {
    if (confirmOpen_ && !ImGui::IsPopupOpen("confirm")) ImGui::OpenPopup("confirm");
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 0, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.42f, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30 * scale_, 26 * scale_));
    if (ImGui::BeginPopupModal("confirm", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        Config& c = Config::get();
        bool game = core_.loaded() && c.editScope == Scope::Game;
        ImGui::PushFont(fontTitle_);
        ImGui::Text("%s", confirmCat_.empty() ? "¿Restaurar todos los ajustes?" : ("¿Restaurar " + confirmCat_ + "?").c_str());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_MUTED, "%s", game ? "Se borrarán los ajustes propios de este juego y volverá a usar los globales."
                                               : "Todas las opciones volverán a sus valores recomendados.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8 * scale_));
        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        bool cancel = (input_.down() & HidNpadButton_B) != 0;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (BigButton("Cancelar", C_PANEL2, w, 0)) cancel = true;
        ImGui::SameLine();
        if (BigButton("Restaurar", C_DANGER, w, 0)) {
            c.resetCategory(confirmCat_.empty() ? nullptr : confirmCat_.c_str());
            toast(confirmCat_.empty() ? "Ajustes restaurados" : confirmCat_ + " restaurado");
            cancel = true;
        }
        if (cancel) { confirmOpen_ = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

void Menu::drawInfo() {
    ImGui::PushFont(fontTitle_); ImGui::Text("Información"); ImGui::PopFont();
    ImGui::TextColored(C_MUTED, "PSX-K2 0.3.0  ·  Beetle PSX HW (GPLv2)  ·  libounce + OunceVK (%s)  ·  salida %dx%d",
                       video_.isVulkan() ? "Vulkan" : "OpenGL", video_.screenW(), video_.screenH());
    ImGui::Dummy(ImVec2(0, 6 * scale_));
    BeginCard("keys", ImGui::GetFontSize() * 9.2f);
    ImGui::TextColored(C_ACCENT2, "Atajos durante el juego");
    ImGui::Text(G_MINUS " + " G_PLUS "            Abrir / cerrar este menú");
    ImGui::Text(G_MINUS " + " G_R "             Guardar estado        " G_MINUS " + " G_L "   Cargar estado");
    ImGui::Text(G_MINUS " + " G_ZR " (mantener)  Avance rápido");
    ImGui::Text(G_MINUS " + " G_Y "             Captura               " G_MINUS " + " G_X "   Siguiente disco");
    ImGui::Text(G_MINUS " + " G_PLUS " 2 s        Salir al hbmenu (emergencia)");
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * scale_));
    BeginCard("dirs", ImGui::GetFontSize() * 7.4f);
    ImGui::TextColored(C_ACCENT2, "Carpetas en la microSD");
    ImGui::TextColored(C_MUTED, "BIOS"); ImGui::SameLine(200 * scale_); ImGui::Text("%s", paths::bios.c_str());
    ImGui::TextColored(C_MUTED, "Juegos"); ImGui::SameLine(200 * scale_); ImGui::Text("%s", paths::roms.c_str());
    ImGui::TextColored(C_MUTED, "Memory cards"); ImGui::SameLine(200 * scale_); ImGui::Text("%s", paths::memcards.c_str());
    ImGui::TextColored(C_MUTED, "Estados"); ImGui::SameLine(200 * scale_); ImGui::Text("%s", paths::states.c_str());
    ImGui::TextColored(C_MUTED, "Capturas"); ImGui::SameLine(200 * scale_); ImGui::Text("%s", paths::screenshots.c_str());
    EndCard();
    ImGui::Dummy(ImVec2(0, 6 * scale_));
    if (BigButton("Salir del emulador", ImVec4(C_DANGER.x * 0.7f, C_DANGER.y * 0.7f, C_DANGER.z * 0.7f, 1), 0, 0)) quit = true;
}

// ------------------------------------------------------------------ frame
void Menu::draw() {
    feedInput();
    ImGuiIO& io = ImGui::GetIO();
    if (!wasOpen_) { openAnim_ = 0.f; wasOpen_ = true; }
    openAnim_ = std::min(1.f, openAnim_ + io.DeltaTime / 0.22f);
    tabAnim_ = std::min(1.f, tabAnim_ + io.DeltaTime / 0.18f);
    float e = easeOut(openAnim_);

    video_.imguiNewFrame();
    ImGui::NewFrame();
    video_.addGameLayer();
    // Oscurece el juego detrás del menú
    if (core_.loaded())
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, col(ImVec4(0, 0, 0, 0.55f), e));

    float m = 0.035f;
    ImVec2 pos(io.DisplaySize.x * m, io.DisplaySize.y * m + (1.f - e) * 40 * scale_);
    ImVec2 size(io.DisplaySize.x * (1 - 2 * m), io.DisplaySize.y * (1 - 2 * m));
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * scale_, 22 * scale_));
    ImGui::PushFont(fontBody_);
    ImGui::Begin("PSX-K2", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    drawHeader();
    ImGui::Dummy(ImVec2(0, 6 * scale_));

    if (wantTab_ >= 0 && wantTab_ != tab_) {
        if (!(wantTab_ == 1 && !core_.loaded())) { tab_ = wantTab_; tabAnim_ = 0.f; ImGui::SetNavCursorVisible(true); }
        else if (wantTab_ == 1) { tab_ = (tab_ == 0) ? 2 : 0; tabAnim_ = 0.f; }   // saltar "Juego" sin juego
    }
    wantTab_ = -1;

    float footerH = ImGui::GetFontSize() * 1.9f;
    float bodyH = ImGui::GetContentRegionAvail().y - footerH;
    float sideW = size.x * 0.20f;
    ImGui::BeginChild("bodyrow", ImVec2(0, bodyH));
    drawSidebar(sideW);
    ImGui::SameLine(0, 22 * scale_);
    float te = easeOut(tabAnim_);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e * (0.35f + 0.65f * te));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (1.f - te) * 24 * scale_);
    ImGui::BeginChild("content", ImVec2(0, 0));
    if (tab_ == 0) drawLibrary(); else if (tab_ == 1) drawGame(); else if (tab_ == 2) drawSettings(); else drawInfo();
    drawConfirm();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    drawFooter();
    ImGui::End();
    drawToast(io.DisplaySize.y - footerH - 40 * scale_);
    ImGui::PopFont();
    ImGui::PopStyleVar(2);

    ImGui::Render();
    video_.imguiRender(ImGui::GetDrawData());
    if ((input_.down() & HidNpadButton_B) && core_.loaded() && !confirmOpen_) open = false;
    if (!open) wasOpen_ = false;
}

void Menu::drawOsd() {
    Config& c = Config::get();
    bool exclusive = !strcmp(c.value("screen_mode"), "exclusive");   // exclusiva: sin superposiciones
    bool fps = c.isOn("show_fps") && !exclusive;
    bool t = !exclusive && !toast_.empty() && armGetSystemTick() < toastUntil_;
    if (!fps && !t && !video_.isVulkan()) return;   // en Vulkan el juego se dibuja con este mismo pase
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)video_.screenW(), (float)video_.screenH());
    io.DeltaTime = 1.f / 60.f;
    wasOpen_ = false;
    video_.imguiNewFrame();
    ImGui::NewFrame();
    video_.addGameLayer();
    ImGui::PushFont(fontBody_);
    if (fps) {
        char b[32]; snprintf(b, sizeof b, "%.0f FPS", core_.fps());
        ImVec2 ts = ImGui::CalcTextSize(b);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 p1(io.DisplaySize.x - 20 * scale_, 20 * scale_ + ts.y + 16 * scale_);
        ImVec2 p0(p1.x - ts.x - 32 * scale_, 20 * scale_);
        ImVec4 fc = core_.fps() >= 58.f ? ImVec4(0.45f, 0.90f, 0.55f, 1) : core_.fps() >= 50.f ? C_WARN : C_DANGER;
        dl->AddRectFilled(p0, p1, col(ImVec4(0, 0, 0, 0.55f)), 30 * scale_);
        dl->AddText(ImVec2(p0.x + 16 * scale_, p0.y + 8 * scale_), col(fc), b);
    }
    if (t) drawToast(io.DisplaySize.y - 40 * scale_);
    ImGui::PopFont();
    ImGui::Render();
    video_.imguiRender(ImGui::GetDrawData());
}
