#pragma once
#include <string>
#include <vector>

class Video; class Input; class CoreHost;
struct ImFont;

// Interfaz con Dear ImGui: biblioteca, juego (pausa), ajustes e información.
// Diseño: barra lateral + contenido + barra de botones; animaciones de entrada y avisos con fundido.
// Mando: cruceta/stick moverse · A aceptar · B volver · L/R secciones · ZL/ZR categorías · ◀▶ cambiar valor.
// Táctil: todo se puede tocar.
class Menu {
public:
    Menu(Video& v, Input& i, CoreHost& c) : video_(v), input_(i), core_(c) {}
    bool init();
    void shutdown();

    void draw();                  // dibuja el menú sobre el último frame
    void drawOsd();               // FPS y avisos durante el juego
    void toast(const std::string& msg);

    bool open = true;
    bool quit = false;
    bool screenshotRequested = false;
    // Carga/cierre diferidos: se ejecutan al inicio del ciclo, nunca a mitad de un frame
    std::string pendingRom;
    bool pendingUnload = false;
    bool pendingReset = false;
    void onGameLoaded(const std::string& rom);
    void setError(const std::string& e) { error_ = e; }

private:
    void feedInput();
    void applyStyle();
    void drawHeader();
    void drawSidebar(float w);
    void drawFooter();
    void drawLibrary();
    void drawGame();
    void drawSettings();
    void drawInfo();
    void drawToast(float bottomY);
    void drawConfirm();
    void drawGameOptions();
    void touchScroll();            // arrastre con el dedo + inercia en la ventana actual
    void refreshStatus(bool force);

    Video& video_; Input& input_; CoreHost& core_;
    std::vector<std::string> roms_;
    bool scanned_ = false;
    std::string error_, toast_;
    unsigned long long toastStart_ = 0, toastUntil_ = 0;
    int tab_ = 0, wantTab_ = -1;
    int cat_ = 0;
    const char* help_ = "";

    ImFont* fontBody_ = nullptr;
    ImFont* fontTitle_ = nullptr;
    float scale_ = 1.f;
    float openAnim_ = 0.f;        // 0..1 animación de entrada
    float tabAnim_ = 1.f;         // fundido al cambiar de sección
    bool wasOpen_ = false;

    // Estado cacheado (antes se consultaba la microSD y la batería en cada frame -> tirones)
    bool biosMissing_ = false;
    bool stateExists_[10] = {};
    unsigned battery_ = 0;
    char clock_[16] = "";
    unsigned long long lastStatus_ = 0, lastBattery_ = 0;
    bool wasOnline_ = false;

    // Táctil
    bool touchDown_ = false, touchDragging_ = false, cancelClick_ = false, longFired_ = false;
    float touchX_ = 0, touchY_ = 0, startX_ = 0, startY_ = 0, dragDY_ = 0, holdTime_ = 0;
    unsigned scrollClaim_ = 0, nextClaim_ = 0;
    float scrollVel_ = 0;

    // Opciones del juego (mantener pulsado / X)
    bool gameOptsOpen_ = false;
    std::string gameOptsRom_;

    // Confirmación de "Restaurar"
    bool confirmOpen_ = false;
    std::string confirmCat_;      // vacío = todas
};
