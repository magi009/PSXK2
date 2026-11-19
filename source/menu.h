#pragma once
#include <string>
#include <vector>

class Video; class Input; class CoreHost;

// Interfaz con Dear ImGui: biblioteca de juegos, menú de pausa y configuración.
// Navegación: cruceta/stick, A confirmar, B volver, L/R cambiar pestaña. También táctil.
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
    void drawLibrary();
    void drawGame();
    void drawSettings();
    void drawInfo();

    Video& video_; Input& input_; CoreHost& core_;
    std::vector<std::string> roms_;
    bool scanned_ = false;
    std::string error_, toast_;
    unsigned long long toastUntil_ = 0;
    int tab_ = 0, wantTab_ = -1;
    const char* help_ = "";
};
