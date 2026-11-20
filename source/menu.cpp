#include "menu.h"
#include "config.h"
#include "core_host.h"
#include "covers.h"
#include "net.h"
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
    io.ConfigNavCursorVisibleAlways = false;   // sin recuadro: el foco se ve con el color del elemento
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
    st.FramePadding = ImVec2(22, 15);
    st.ItemSpacing = ImVec2(18, 16);
    st.ItemInnerSpacing = ImVec2(10, 8);
    st.ScrollbarSize = 16;
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
    c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);   // sin recuadro azul/verde
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

// Lo que antes se consultaba cada frame (stat() en la microSD, IPC de batería) ahora se refresca
// solo cada cierto tiempo o cuando cambia algo: eran la causa de los tirones del menú.
void Menu::refreshStatus(bool force) {
    u64 now = armGetSystemTick();
    if (force || armTicksToNs(now - lastStatus_) > 3000000000ULL) {
        lastStatus_ = now;
        biosMissing_ = !paths::missingBios().empty();
        for (int i = 0; i < 10; i++) stateExists_[i] = core_.loaded() && core_.stateExists(i);
        bool on = net::online();
        if (on && !wasOnline_) covers::retryMissing();
        wasOnline_ = on;
    }
    if (force || armTicksToNs(now - lastBattery_) > 30000000000ULL || !lastBattery_) {
        lastBattery_ = now;
        u32 b = 0; if (R_SUCCEEDED(psmGetBatteryChargePercentage(&b))) battery_ = b;
    }
    time_t t = time(nullptr);
    strftime(clock_, sizeof clock_, "%H:%M", localtime(&t));
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

    // Táctil: un toque es un clic; si el dedo se mueve más de 14 px es un arrastre (desplaza la lista
    // y NO pulsa nada); mantener 0,6 s sin moverse abre las opciones del juego.
    HidTouchScreenState ts = {};
    bool down = hidGetTouchScreenStates(&ts, 1) && ts.count > 0;
    float tx = down ? ts.touches[0].x * io.DisplaySize.x / 1280.f : touchX_;
    float ty = down ? ts.touches[0].y * io.DisplaySize.y / 720.f : touchY_;
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    if (down && !touchDown_) {                       // empieza el toque
        startX_ = tx; startY_ = ty; touchDragging_ = false; holdTime_ = 0; longFired_ = false; cancelClick_ = false;
        scrollVel_ = 0; scrollClaim_ = nextClaim_ = 0;
    }
    dragDY_ = down && touchDown_ ? ty - touchY_ : 0;
    if (down) {
        holdTime_ += io.DeltaTime;
        if (!touchDragging_ && (fabsf(tx - startX_) > 14 * scale_ || fabsf(ty - startY_) > 14 * scale_)) {
            touchDragging_ = true; cancelClick_ = true;
        }
    }
    touchX_ = tx; touchY_ = ty;
    if (cancelClick_) {
        // Se aparta el puntero antes de soltar: así ImGui no cuenta el arrastre como pulsación
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        io.AddMouseButtonEvent(0, false);
    } else if (down) {
        io.AddMousePosEvent(tx, ty);
        io.AddMouseButtonEvent(0, true);
    } else {
        io.AddMouseButtonEvent(0, false);
        if (touchDown_) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);   // sin "hover" pegado tras soltar
    }
    if (!down && !touchDown_) { cancelClick_ = false; longFired_ = false; }   // sin toque: estado limpio
    touchDown_ = down;
    // Cualquier pulsación del mando vuelve a mostrar el foco (y lo devuelve al contenido)
    if (input_.down() & (HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_AnyLeft | HidNpadButton_AnyRight |
                         HidNpadButton_A | HidNpadButton_X)) {
        ImGui::SetNavCursorVisible(true);
        if (sidebarFocused_) refocusContent_ = true;
    }

    u64 d = input_.down();
    if (!confirmOpen_ && !gameOptsOpen_) {
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

// Arrastre con el dedo en la ventana actual. Si hay ventanas anidadas, se queda con la más interna
// que tenga contenido desplazable. Al soltar sigue un poco por inercia.
void Menu::touchScroll() {
    ImGuiIO& io = ImGui::GetIO();
    unsigned id = ImGui::GetID("##touchscroll");
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    bool inside = startX_ >= wp.x && startX_ < wp.x + ws.x && startY_ >= wp.y && startY_ < wp.y + ws.y;
    if (touchDragging_ && inside && ImGui::GetScrollMaxY() > 0 && scrollClaim_ == 0) nextClaim_ = id;
    // Stick derecho: desplaza la lista de la sección actual (velocidad proporcional a la inclinación)
    HidAnalogStickState rs = input_.stick(0, 1);
    float ry = rs.y / 32767.f;
    if (fabsf(ry) > 0.18f) {
        float sp = (fabsf(ry) - 0.18f) / 0.82f;
        ImGui::SetScrollY(ImGui::GetScrollY() - (ry > 0 ? 1.f : -1.f) * sp * sp * 2200.f * scale_ * io.DeltaTime);
    }
    if (id != scrollClaim_) return;
    if (touchDown_ && touchDragging_) {
        ImGui::SetScrollY(ImGui::GetScrollY() - dragDY_);
        if (io.DeltaTime > 0) scrollVel_ = scrollVel_ * 0.6f + (-dragDY_ / io.DeltaTime) * 0.4f;
    } else if (fabsf(scrollVel_) > 20.f) {
        ImGui::SetScrollY(ImGui::GetScrollY() + scrollVel_ * io.DeltaTime);
        scrollVel_ *= powf(0.04f, io.DeltaTime);      // frenado suave
    } else {
        scrollVel_ = 0;
    }
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
    char right[64];
    snprintf(right, sizeof right, "%s%s     %u%%", wasOnline_ ? "\u25CF En línea     " : "", clock_, battery_);
    float w = ImGui::CalcTextSize(right).x;
    float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(rightEdge - w);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.45f);
    ImGui::TextColored(C_TEXT, "%s", right);
}

void Menu::drawSidebar(float w) {
    static const char* kNames[5] = {"Biblioteca", "Juego", "Ajustes", "Información", "Salir"};
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * scale_, 18 * scale_));
    // Sin navegación por mando: las secciones se cambian con L/R (o tocando)
    ImGui::BeginChild("side", ImVec2(w, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float itemH = ImGui::GetFontSize() * 2.5f;
    for (int i = 0; i < 5; i++) {
        bool exit = i == 4;
        if (exit) ImGui::Dummy(ImVec2(0, 14 * scale_));     // separado del resto
        bool sel = tab_ == i;
        bool disabled = (i == 1 && !core_.loaded());
        ImVec2 p = ImGui::GetCursorScreenPos();
        float iw = ImGui::GetContentRegionAvail().x;
        bool hov = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + iw, p.y + itemH));
        if (sel) {
            dl->AddRectFilled(p, ImVec2(p.x + iw, p.y + itemH), col(C_ACCENT, 0.28f), 12 * scale_);
            dl->AddRectFilled(p, ImVec2(p.x + 6 * scale_, p.y + itemH), col(C_ACCENT), 3 * scale_);
        } else if (hov && !disabled) {
            dl->AddRectFilled(p, ImVec2(p.x + iw, p.y + itemH), col(C_PANEL2), 12 * scale_);
        }
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("tab", ImVec2(iw, itemH)) && !disabled) {
            if (exit) { confirmOpen_ = true; confirmCat_ = "__salir__"; }
            else wantTab_ = i;
            refocusContent_ = true;
        }
        ImGui::PopID();
        ImVec2 ts = ImGui::CalcTextSize(kNames[i]);
        ImVec4 tc = exit ? ImVec4(1.f, 0.55f, 0.55f, 1) : disabled ? C_MUTED : sel ? C_TEXT : ImVec4(0.75f, 0.78f, 0.85f, 1);
        dl->AddText(ImVec2(p.x + 24 * scale_, p.y + (itemH - ts.y) * 0.5f), col(tc, disabled ? 0.5f : 1.f), kNames[i]);
        ImGui::Dummy(ImVec2(0, 6 * scale_));
    }
    sidebarFocused_ = ImGui::IsWindowFocused();
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
    if (tab_ == 0) hints += "     " G_X " Opciones (o mantén pulsado)";
    if (tab_ == 2) hints += "     " G_ZL " " G_ZR " Categoría     ◀ ▶ Cambiar";
    hints += "     " G_L " " G_R " Secciones     Stick dcho. Desplazar";
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
    if (!scanned_) { roms_ = paths::scanRoms(); scanned_ = true; refreshStatus(true); }
    Config& cfg = Config::get();
    bool grid = !strcmp(cfg.value("library_view"), "grid");
    ImGui::PushFont(fontTitle_); ImGui::Text("Biblioteca"); ImGui::PopFont();
    float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::TextColored(C_MUTED, "%zu juegos", roms_.size());
    std::string cm = covers::lastMessage();
    if (!cm.empty()) { ImGui::SameLine(); ImGui::TextColored(C_MUTED, "·  %s", cm.c_str()); }
    const char* viewLbl = grid ? "Ver en lista" : "Ver portadas";
    float bw1 = ImGui::CalcTextSize(viewLbl).x + 36 * scale_, bw2 = ImGui::CalcTextSize("Actualizar").x + 36 * scale_;
    ImGui::SameLine(rightEdge - bw1 - bw2 - 12 * scale_);
    if (ImGui::Button(viewLbl)) cfg.setIndex("library_view", grid ? 1 : 0);
    ImGui::SameLine();
    if (ImGui::Button("Actualizar")) { scanned_ = false; covers::retryMissing(); }

    if (biosMissing_) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.30f, 0.20f, 0.08f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * scale_, 12 * scale_));
        ImGui::BeginChild("bios", ImVec2(0, ImGui::GetFontSize() * 3.4f), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(C_WARN, "Falta la BIOS de PlayStation");
        ImGui::TextWrapped("Copia scph5501.bin (USA), scph5500.bin (Japón) o scph5502.bin (Europa) en %s", paths::bios.c_str());
        ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor();
    }
    if (!error_.empty()) ImGui::TextColored(C_DANGER, "%s", error_.c_str());
    ImGui::Dummy(ImVec2(0, 4 * scale_));

    ImGui::BeginChild("roms", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollWithMouse);
    touchScroll();
    if (roms_.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_TEXT, "Todavía no hay juegos.");
        ImGui::TextColored(C_MUTED, "1. Conecta la microSD al PC.\n2. Copia tus juegos (.chd, .cue + .bin, .pbp o .m3u) en %s\n"
                                    "3. Vuelve aquí y toca \"Actualizar\".", paths::roms.c_str());
        ImGui::PopTextWrapPos();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    u64 d = input_.down();
    int cols = grid ? 4 : 1;               // 4 por fila: más aire y portadas más grandes
    float gap = 28 * scale_;
    float availW = ImGui::GetContentRegionAvail().x;
    float cardW = grid ? (availW - gap * (cols - 1)) / cols : availW;
    float coverH = grid ? cardW : ImGui::GetFontSize() * 2.6f;
    float cardH = coverH;
    for (size_t i = 0; i < roms_.size(); i++) {
        const std::string& r = roms_[i];
        std::string name = paths::stem(r);
        // Cuadrícula: la fila crece lo necesario para mostrar el nombre COMPLETO del juego más largo
        if (grid && i % cols == 0) {
            float th = 0;
            for (size_t k = i; k < roms_.size() && k < i + cols; k++)
                th = std::max(th, ImGui::CalcTextSize(paths::stem(roms_[k]).c_str(), nullptr, false, cardW - 8 * scale_).y);
            cardH = coverH + th + 22 * scale_;
        }
        ImGui::PushID((int)i);
        if (grid && i % cols) ImGui::SameLine(0, gap);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::Selectable("##rom", false, 0, ImVec2(cardW, cardH));
        bool hovered = ImGui::IsItemHovered();
        bool focused = (ImGui::IsItemFocused() && io.NavVisible) || hovered;
        bool visible = ImGui::IsRectVisible(p, ImVec2(p.x + cardW, p.y + cardH));
        // Mantener pulsado (táctil) o X (mando) = opciones del juego
        if (hovered && touchDown_ && !touchDragging_ && holdTime_ > 0.6f && !longFired_) {
            longFired_ = true; cancelClick_ = true; gameOptsOpen_ = true; gameOptsRom_ = r;
        }
        if (ImGui::IsItemFocused() && (d & HidNpadButton_X)) { gameOptsOpen_ = true; gameOptsRom_ = r; }
        if (clicked && !longFired_) pendingRom = r;
        if (visible) {
            std::string ext = paths::ext(r).substr(1);
            for (auto& ch : ext) ch = (char)toupper(ch);
            float rad = 14 * scale_;
            if (grid) {
                ImVec2 c0 = p, c1(p.x + cardW, p.y + coverH);
                float asp = 1.f;
                covers::State st;
                uint64_t tex = covers::get(name, &asp, &st);
                if (focused) dl->AddRectFilled(ImVec2(p.x - 6 * scale_, p.y - 6 * scale_), ImVec2(p.x + cardW + 6 * scale_, p.y + cardH),
                                               col(C_ACCENT, 0.35f), rad + 4 * scale_);
                if (tex) {
                    // Encaja la portada en el cuadro sin deformarla
                    float bw = cardW, bh = coverH;
                    if (asp > 1.f) bh = cardW / asp; else bw = coverH * asp;
                    ImVec2 i0(p.x + (cardW - bw) * 0.5f, p.y + (coverH - bh) * 0.5f);
                    dl->AddRectFilled(c0, c1, col(ImVec4(0.06f, 0.07f, 0.10f, 1)), rad);
                    dl->AddImageRounded((ImTextureID)(uintptr_t)tex, i0, ImVec2(i0.x + bw, i0.y + bh), ImVec2(0, 0), ImVec2(1, 1),
                                        IM_COL32_WHITE, rad);
                } else {
                    // Portada provisional: degradado con la inicial del juego
                    dl->AddRectFilledMultiColor(c0, c1, col(ImVec4(0.22f, 0.20f, 0.42f, 1)), col(ImVec4(0.12f, 0.30f, 0.42f, 1)),
                                                col(ImVec4(0.08f, 0.10f, 0.18f, 1)), col(ImVec4(0.14f, 0.12f, 0.26f, 1)));
                    char ini[2] = {name.empty() ? '?' : (char)toupper(name[0]), 0};
                    ImGui::PushFont(fontTitle_);
                    ImVec2 isz = ImGui::CalcTextSize(ini);
                    dl->AddText(fontTitle_, fontTitle_->FontSize * 1.6f, ImVec2(p.x + (cardW - isz.x * 1.6f) * 0.5f, p.y + (coverH - isz.y * 1.6f) * 0.45f),
                                col(C_TEXT, 0.85f), ini);
                    ImGui::PopFont();
                    if (st == covers::State::Loading)
                        dl->AddText(ImVec2(p.x + 12 * scale_, p.y + coverH - ImGui::GetFontSize() - 10 * scale_), col(C_MUTED), "Buscando portada...");
                }
                // Insignia de formato
                ImVec2 es = ImGui::CalcTextSize(ext.c_str());
                ImVec2 b0(p.x + 10 * scale_, p.y + 10 * scale_);
                dl->AddRectFilled(b0, ImVec2(b0.x + es.x + 16 * scale_, b0.y + es.y + 6 * scale_), col(ImVec4(0, 0, 0, 0.6f)), 8 * scale_);
                dl->AddText(ImVec2(b0.x + 8 * scale_, b0.y + 3 * scale_), col(C_TEXT), ext.c_str());
                // Título completo (la altura de la fila ya se ajustó a él)
                dl->AddText(nullptr, 0.f, ImVec2(p.x + 4 * scale_, p.y + coverH + 10 * scale_), col(focused ? C_TEXT : ImVec4(0.80f, 0.83f, 0.90f, 1)),
                            name.c_str(), nullptr, cardW - 8 * scale_);
            } else {
                dl->AddRectFilled(p, ImVec2(p.x + cardW, p.y + cardH), col(focused ? C_PANEL2 : C_PANEL), 12 * scale_);
                float asp = 1.f;
                uint64_t tex = covers::get(name, &asp);
                float th = cardH - 12 * scale_, tw = th;
                ImVec2 t0(p.x + 8 * scale_, p.y + 6 * scale_);
                if (tex) dl->AddImageRounded((ImTextureID)(uintptr_t)tex, t0, ImVec2(t0.x + tw, t0.y + th), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8 * scale_);
                else dl->AddRectFilled(t0, ImVec2(t0.x + tw, t0.y + th), col(ImVec4(0.20f, 0.20f, 0.36f, 1)), 8 * scale_);
                ImVec2 es = ImGui::CalcTextSize(ext.c_str());
                float bx = t0.x + tw + 18 * scale_, by = p.y + (cardH - es.y) * 0.5f;
                dl->AddRectFilled(ImVec2(bx - 8 * scale_, by - 4 * scale_), ImVec2(bx + es.x + 8 * scale_, by + es.y + 4 * scale_),
                                  col(focused ? C_ACCENT : ImVec4(0.25f, 0.27f, 0.36f, 1)), 8 * scale_);
                dl->AddText(ImVec2(bx, by), col(C_TEXT), ext.c_str());
                dl->AddText(ImVec2(bx + es.x + 30 * scale_, p.y + (cardH - ImGui::GetFontSize()) * 0.5f), col(C_TEXT), name.c_str());
            }
        }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 20 * scale_));
    ImGui::EndChild();
}

void Menu::drawGameOptions() {
    if (gameOptsOpen_ && !ImGui::IsPopupOpen("gameopts")) ImGui::OpenPopup("gameopts");
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 0, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.52f, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30 * scale_, 26 * scale_));
    if (ImGui::BeginPopupModal("gameopts", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string name = paths::stem(gameOptsRom_);
        float asp = 1.f;
        uint64_t tex = covers::get(name, &asp);
        float ch = ImGui::GetFontSize() * 7.f;
        if (tex) { ImGui::Image((ImTextureID)(uintptr_t)tex, ImVec2(ch * asp, ch)); ImGui::SameLine(); }
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::PushFont(fontTitle_); ImGui::TextWrapped("%s", name.c_str()); ImGui::PopFont();
        ImGui::TextColored(C_MUTED, "%s", paths::ext(gameOptsRom_).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, 6 * scale_));
        float W = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
        float third = (W - 2 * gap) / 3.f, bh = ImGui::GetFontSize() * 2.2f;
        bool close = (input_.down() & HidNpadButton_B) != 0;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (BigButton("Jugar", C_ACCENT, W, bh * 1.1f)) { pendingRom = gameOptsRom_; close = true; }
        ImGui::Dummy(ImVec2(0, 4 * scale_));
        bool online = wasOnline_;
        ImGui::TextColored(C_ACCENT2, "Cambiar portada");
        ImGui::SameLine(); ImGui::TextColored(C_MUTED, online ? "(se descarga de internet)" : "(sin internet)");
        ImGui::BeginDisabled(!online);
        if (BigButton("Caja", C_PANEL2, third, bh)) { covers::replace(name, covers::Kind::Box); toast("Descargando portada (caja)..."); }
        ImGui::SameLine();
        if (BigButton("Pantalla de título", C_PANEL2, third, bh)) { covers::replace(name, covers::Kind::Title); toast("Descargando pantalla de título..."); }
        ImGui::SameLine();
        if (BigButton("Captura", C_PANEL2, third, bh)) { covers::replace(name, covers::Kind::Snap); toast("Descargando captura..."); }
        ImGui::EndDisabled();
        if (BigButton("Quitar portada", C_PANEL, W, bh)) { covers::remove(name); toast("Portada quitada"); }
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_MUTED, "Para usar tu propia imagen, copia desde el PC un archivo .png o .jpg llamado \"%s\" en %s",
                           name.c_str(), covers::folder().c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4 * scale_));
        if (BigButton("Cerrar", C_PANEL2, W, bh)) close = true;
        if (close) { gameOptsOpen_ = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
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
    // Velocidad: normal (Play) o avance rápido x3, uno al lado del otro
    if (BigButton("\u25B6  Velocidad normal", turbo ? C_PANEL2 : ImVec4(0.18f, 0.45f, 0.32f, 1), half, bh)) {
        turbo = false; toast("Velocidad normal"); open = false;
    }
    ImGui::SameLine();
    if (BigButton("\u25B6\u25B6  Avance rápido x3", turbo ? ImVec4(0.18f, 0.45f, 0.32f, 1) : C_PANEL2, half, bh)) {
        turbo = true; toast("Avance rápido x3"); open = false;
    }
    ImGui::Dummy(ImVec2(0, 6 * scale_));
    if (BigButton("Reiniciar", C_PANEL2, half, bh)) { pendingReset = true; open = false; }
    ImGui::SameLine();
    if (BigButton("Captura de pantalla", C_PANEL2, half, bh)) { open = false; screenshotRequested = true; }

    snprintf(buf, sizeof buf, "Guardar estado  ·  ranura %d", slot);
    if (BigButton(buf, C_PANEL2, half, bh)) {
        toast(core_.saveState(slot) ? "Estado guardado en la ranura " + std::to_string(slot) : "Error al guardar el estado");
        refreshStatus(true);
    }
    ImGui::SameLine();
    snprintf(buf, sizeof buf, "Cargar estado  ·  ranura %d%s", slot, stateExists_[slot] ? "" : " (vacía)");
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
        pendingUnload = true; wantTab_ = 0; turbo = false;
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
    float chipsEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < (int)cats.size(); i++) {
        if (i) ImGui::SameLine(0, 8 * scale_);
        if (i && ImGui::GetCursorPosX() + ImGui::CalcTextSize(cats[i].c_str()).x + 40 * scale_ > chipsEdge)
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
    ImGui::BeginChild("opts", ImVec2(0, -helpH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollWithMouse);
    touchScroll();
    help_ = "";
    ImGuiIO& io = ImGui::GetIO();
    float rowH = ImGui::GetFontSize() * 2.6f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (auto& o : c.defs()) {
        if (cats[cat_] != o.category) continue;
        if (!c.appliesToRenderer(o)) continue;          // no se muestra lo que no funciona con este renderer
        int idx = c.index(o.id), n = (int)o.choices.size();
        ImGui::PushID(o.id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        bool act = ImGui::Selectable("##opt", false, 0, ImVec2(w, rowH));
        bool foc = (ImGui::IsItemFocused() && io.NavVisible) || ImGui::IsItemHovered();
        bool viaTouch = act && !io.NavVisible;
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
        if ((ImGui::IsItemFocused() || ImGui::IsItemHovered()) && *o.help) help_ = o.help;
        if (ImGui::IsItemFocused() && io.NavVisible && !confirmOpen_) {
            if (d & HidNpadButton_Right) c.setIndex(o.id, (idx + 1) % n);
            if (d & HidNpadButton_Left) c.setIndex(o.id, (idx + n - 1) % n);
        }
        if (act) {
            // Táctil: tocar la mitad izquierda de la píldora resta, la derecha suma
            float mx = io.MouseClickedPos[0].x;
            bool left = viaTouch && mx >= v0.x && mx < (v0.x + v1.x) * 0.5f;
            c.setIndex(o.id, left ? (idx + n - 1) % n : (idx + 1) % n);
        }
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
        bool exitApp = confirmCat_ == "__salir__";
        ImGui::PushFont(fontTitle_);
        if (exitApp) ImGui::Text("¿Salir de PSX-K2?");
        else ImGui::Text("%s", confirmCat_.empty() ? "¿Restaurar todos los ajustes?" : ("¿Restaurar " + confirmCat_ + "?").c_str());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_MUTED, "%s", exitApp ? "Se guardará la memory card y volverás al menú de homebrew."
                                        : game ? "Se borrarán los ajustes propios de este juego y volverá a usar los globales."
                                               : "Todas las opciones volverán a sus valores recomendados.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8 * scale_));
        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        bool cancel = (input_.down() & HidNpadButton_B) != 0;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (BigButton("Cancelar", C_PANEL2, w, 0)) cancel = true;
        ImGui::SameLine();
        if (exitApp) {
            if (BigButton("Salir", C_DANGER, w, 0)) { quit = true; cancel = true; }
        } else if (BigButton("Restaurar", C_DANGER, w, 0)) {
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
    ImGui::TextColored(C_MUTED, "PSX-K2 0.6.0  ·  Beetle PSX HW (GPLv2)  ·  libounce + OunceVK (%s)  ·  salida %dx%d",
                       video_.isVulkan() ? "Vulkan" : "OpenGL", video_.screenW(), video_.screenH());
    ImGui::TextColored(C_MUTED, "Pantalla: %s", video_.refreshStatus());
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

}

// ------------------------------------------------------------------ frame
void Menu::draw() {
    feedInput();
    refreshStatus(false);
    covers::pump();
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
    if (refocusContent_ || sidebarFocused_) { ImGui::SetNextWindowFocus(); refocusContent_ = false; }
    ImGui::BeginChild("content", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollWithMouse);
    if (tab_ != 0 && tab_ != 2) touchScroll();
    if (tab_ == 0) drawLibrary(); else if (tab_ == 1) drawGame(); else if (tab_ == 2) drawSettings(); else drawInfo();
    drawConfirm();
    drawGameOptions();
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
    if ((input_.down() & HidNpadButton_B) && core_.loaded() && !confirmOpen_ && !gameOptsOpen_) open = false;
    if (!open) wasOpen_ = false;
    if (nextClaim_ && !scrollClaim_) scrollClaim_ = nextClaim_;   // la ventana que se arrastra queda fijada
}

void Menu::drawOsd() {
    Config& c = Config::get();
    bool exclusive = !strcmp(c.value("screen_mode"), "exclusive");   // exclusiva: sin superposiciones
    bool fps = (c.isOn("show_fps") || turbo) && !exclusive;
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
        char b[48];
        if (turbo) snprintf(b, sizeof b, "\u25B6\u25B6 x3   %.0f FPS", core_.fps());
        else snprintf(b, sizeof b, "%.0f FPS", core_.fps());
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
