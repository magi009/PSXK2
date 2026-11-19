#pragma once
#include <switch.h>
#include <cstdint>

// Atajos (mantener  −  y pulsar):
//   − + +   abrir/cerrar menú         − + ZR  avance rápido (mantener)
//   − + +   MANTENER 2 s: salida de emergencia al hbmenu (guarda la memory card)
//   − + R   guardar estado rápido     − + L   cargar estado rápido
//   − + Y   captura de pantalla       − + X   cambiar disco (siguiente)
enum class Hotkey { None, Menu, SaveState, LoadState, Screenshot, NextDisc };

class Input {
public:
    void init();
    void update();
    int16_t state(unsigned port, unsigned device, unsigned index, unsigned id) const;
    Hotkey hotkey() const { return hotkey_; }
    bool fastForward() const { return ff_; }
    bool quitRequested() const { return quit_; }
    void rumble(unsigned port, bool strong, uint16_t strength);

    // Para el menú
    u64 down(int port = 0) const { return padGetButtonsDown(&pads_[port]); }
    u64 held(int port = 0) const { return padGetButtons(&pads_[port]); }
    HidAnalogStickState stick(int port, int s) const { return padGetStickPos(&pads_[port], s); }

private:
    PadState pads_[2];
    Hotkey hotkey_ = Hotkey::None;
    bool ff_ = false;
    bool minusCombo_ = false;
    bool quit_ = false;
    u64 comboStart_ = 0;
    HidVibrationDeviceHandle vib_[2][2];
    float vibLo_[2] = {}, vibHi_[2] = {};
};
