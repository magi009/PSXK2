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

    // Confirmación de "Restaurar"
    bool confirmOpen_ = false;
    std::string confirmCat_;      // vacío = todas
};
