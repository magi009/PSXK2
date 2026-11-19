// PSX-K2 — Emulador de PlayStation para Switch 2 con kaiHEN (libounce + OunceVK), formato .nro
#include "audio.h"
#include "config.h"
#include "core_host.h"
#include "covers.h"
#include "net.h"
#include "input.h"
#include "log.h"
#include "menu.h"
#include "paths.h"
#include "video.h"
#include <switch.h>
#include <cstring>
#include <string>

int main(int, char**) {
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleep);
    paths::init();
    logx::init((paths::logs + "/psx-k2.log").c_str());
    LOGI("PSX-K2 iniciando");

    // Reparto de núcleos de CPU: los hilos que crea el núcleo (GPU, texturas, portadas) nacen con
    // menor prioridad y prefieren el núcleo por defecto del proceso. Si el hilo principal (emulación,
    // siempre ocupado) se queda ahí, el hilo de GPU no consigue CPU y todo va a pocos fps.
    // Por eso el hilo principal se mueve a otro núcleo y deja libre el de por defecto.
    {
        u64 mask = 0;
        svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
        int cur = svcGetCurrentProcessorNumber();
        int target = -1;
        for (int c = 1; c < 8 && target < 0; c++) if ((mask & (1ULL << c)) && c != cur) target = c;
        if (target >= 0 && R_SUCCEEDED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, target, (u32)(1u << target))))
            LOGI("[cpu] núcleos disponibles 0x%llx; hilo principal: núcleo %d -> %d", (unsigned long long)mask, cur, target);
        else
            LOGI("[cpu] núcleos disponibles 0x%llx; hilo principal se queda en el núcleo %d", (unsigned long long)mask, cur);
    }

    Config& cfg = Config::get();
    cfg.loadGlobal();

    Video video;
    if (!video.init()) { logx::shutdown(); return 1; }
    Audio audio; audio.init();
    Input input; input.init();
    CoreHost core(video, audio, input);
    core.init();
    Menu menu(video, input, core);
    menu.init();
    net::init();
    covers::init(video);

    AppletOperationMode lastMode = appletGetOperationMode();
    bool shotPending = false;
    u64 beatTick = armGetSystemTick(); unsigned beatFrames = 0;
    // Protección de rendimiento
    unsigned lastFpsSerial = 0, slowSeconds = 0; double lastGuardAction = -100;

    while (appletMainLoop() && !menu.quit) {
        input.update();
        if (input.quitRequested()) { LOGI("salida de emergencia (− + + 2 s)"); break; }

        // Carga/cierre de juego fuera de cualquier frame (Vulkan puede cambiar de dispositivo)
        if (menu.pendingUnload) { menu.pendingUnload = false; core.unloadGame(); }
        if (menu.pendingReset) { menu.pendingReset = false; core.reset(); }
        if (!menu.pendingRom.empty()) {
            std::string rom = menu.pendingRom, err;
            menu.pendingRom.clear();
            if (core.loadGame(rom, err)) menu.onGameLoaded(rom); else menu.setError(err);
        }

        // Cambio portátil <-> dock o de opciones del frontend
        AppletOperationMode mode = appletGetOperationMode();
        if (mode != lastMode || cfg.consumeFrontendDirty()) { lastMode = mode; video.applyOutputMode(); }
        video.beginFrame();

        // El atajo del menú se procesa UNA vez por frame (antes se abría y cerraba en el mismo frame)
        if (input.hotkey() == Hotkey::Menu && core.loaded()) menu.open = !menu.open;
        else if (core.loaded() && !menu.open) {
            switch (input.hotkey()) {
            case Hotkey::SaveState: menu.toast(core.saveState(cfg.index("state_slot")) ? "Estado guardado" : "Error al guardar"); break;
            case Hotkey::LoadState: menu.toast(core.loadState(cfg.index("state_slot")) ? "Estado cargado" : "Ranura vacía"); break;
            case Hotkey::Screenshot: shotPending = true; break;
            case Hotkey::NextDisc:
                if (core.discCount() > 1) {
                    unsigned n = (core.discIndex() + 1) % core.discCount();
                    menu.toast(core.swapDisc(n) ? "Disco " + std::to_string(n + 1) : "No se pudo cambiar disco");
                }
                break;
            default: break;
            }
        }
        // Protección de rendimiento: si el juego no aguanta 57 fps durante 3 s seguidos, baja un nivel
        // la resolución interna de ESTE juego. Se ignoran los primeros 15 s (compilación de shaders).
        if (core.loaded() && !menu.open && !input.fastForward() && !menu.turbo && core.fpsSerial() != lastFpsSerial) {
            lastFpsSerial = core.fpsSerial();
            double t = core.secondsSinceLoad();
            slowSeconds = (t > 15 && core.fps() < 57.f) ? slowSeconds + 1 : 0;
            const char* guard = cfg.value("perf_guard");
            if (slowSeconds >= 3 && strcmp(guard, "off") && t - lastGuardAction > 10) {
                lastGuardAction = t; slowSeconds = 0;
                int res = cfg.index("internal_res"), msaa = cfg.index("msaa");
                bool vk = !strcmp(cfg.value("renderer"), "hardware_vk");
                if (!strcmp(guard, "auto") && ((vk && msaa > 0) || res > 0)) {
                    Scope prev = cfg.editScope;
                    cfg.editScope = Scope::Game;
                    if (vk && msaa > 0) {              // el MSAA es lo más caro: se baja primero
                        cfg.setIndex("msaa", msaa - 1);
                        LOGI("[perf] %.1f fps: MSAA %d -> %d", core.fps(), msaa, msaa - 1);
                        menu.toast(msaa - 1 ? "Rendimiento: MSAA bajado a " + std::to_string(1 << (msaa - 1)) + "x"
                                            : std::string("Rendimiento: MSAA desactivado"));
                    } else {
                        cfg.setIndex("internal_res", res - 1);
                        LOGI("[perf] %.1f fps: resolución interna %d -> %d", core.fps(), res, res - 1);
                        menu.toast("Rendimiento: resolución interna bajada a " + std::to_string(1 << (res - 1)) + "x");
                    }
                    cfg.editScope = prev;
                } else {
                    menu.toast("Rendimiento bajo (" + std::to_string((int)core.fps()) + " fps): baja la resolución o el MSAA");
                }
            }
        }

        if (menu.screenshotRequested) { shotPending = true; menu.screenshotRequested = false; }

        if (core.loaded() && !menu.open) {
            core.runFrame(input.fastForward() || menu.turbo);
            if (shotPending) {               // se captura antes de dibujar el OSD
                std::string p;
                menu.toast(core.takeScreenshot(p) ? "Captura guardada" : "Error en captura");
                shotPending = false;
            }
            menu.drawOsd();
        } else {
            if (core.loaded()) video.redrawLast(); else video.clearScreen();
            menu.draw();
        }
        video.swap();
        beatFrames++;
        if (armTicksToNs(armGetSystemTick() - beatTick) >= 5000000000ULL) {
            LOGI("[main] vivo: %u ciclos en 5 s, juego %s (%.1f fps), menú %s", beatFrames,
                 core.loaded() ? "sí" : "no", core.fps(), menu.open ? "abierto" : "cerrado");
            beatTick = armGetSystemTick(); beatFrames = 0;
        }
    }

    cfg.saveGlobal();
    covers::shutdown();
    core.deinit();
    menu.shutdown();
    net::shutdown();
    audio.shutdown();
    video.shutdown();
    LOGI("PSX-K2 cerrado");
    logx::shutdown();
    return 0;
}
