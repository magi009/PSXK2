#include "input.h"
#include "config.h"
#include <libretro.h>
#include <cstring>

void Input::init() {
    padConfigureInput(2, HidNpadStyleSet_NpadStandard);
    padInitialize(&pads_[0], HidNpadIdType_No1, HidNpadIdType_Handheld);
    padInitialize(&pads_[1], HidNpadIdType_No2);
    hidInitializeVibrationDevices(vib_[0], 2, HidNpadIdType_No1, (HidNpadStyleTag)(HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadJoyDual));
    hidInitializeVibrationDevices(vib_[1], 2, HidNpadIdType_No2, (HidNpadStyleTag)(HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadJoyDual));
}

void Input::update() {
    padUpdate(&pads_[0]);
    padUpdate(&pads_[1]);
    u64 h = padGetButtons(&pads_[0]);
    u64 d = padGetButtonsDown(&pads_[0]);
    hotkey_ = Hotkey::None;
    ff_ = false;
    if (h & HidNpadButton_Minus) {
        if (d & HidNpadButton_Plus) hotkey_ = Hotkey::Menu;
        else if (d & HidNpadButton_R) hotkey_ = Hotkey::SaveState;
        else if (d & HidNpadButton_L) hotkey_ = Hotkey::LoadState;
        else if (d & HidNpadButton_Y) hotkey_ = Hotkey::Screenshot;
        else if (d & HidNpadButton_X) hotkey_ = Hotkey::NextDisc;
        if (h & HidNpadButton_ZR) ff_ = true;
        if (hotkey_ != Hotkey::None || ff_) minusCombo_ = true;
    } else {
        minusCombo_ = false;
    }
}

int16_t Input::state(unsigned port, unsigned device, unsigned index, unsigned id) const {
    if (port > 1) return 0;
    const PadState& p = pads_[port];
    u64 b = padGetButtons(&p);
    unsigned base = device & RETRO_DEVICE_MASK;

    if (base == RETRO_DEVICE_JOYPAD) {
        // Mientras se usa − como atajo no se envía SELECT ni los botones del atajo
        if (port == 0 && minusCombo_) b &= ~(HidNpadButton_Minus | HidNpadButton_Plus | HidNpadButton_ZR |
                                             HidNpadButton_R | HidNpadButton_L | HidNpadButton_X | HidNpadButton_Y);
        bool nintendo = !strcmp(Config::get().value("face_layout"), "nintendo");
        u64 cross    = nintendo ? HidNpadButton_A : HidNpadButton_B;
        u64 circle   = nintendo ? HidNpadButton_B : HidNpadButton_A;
        u64 square   = nintendo ? HidNpadButton_X : HidNpadButton_Y;
        u64 triangle = nintendo ? HidNpadButton_Y : HidNpadButton_X;
        u64 m = 0;
        switch (id) {
            case RETRO_DEVICE_ID_JOYPAD_B:      m = cross; break;     // X (cruz)
            case RETRO_DEVICE_ID_JOYPAD_A:      m = circle; break;    // O
            case RETRO_DEVICE_ID_JOYPAD_Y:      m = square; break;    // □
            case RETRO_DEVICE_ID_JOYPAD_X:      m = triangle; break;  // △
            case RETRO_DEVICE_ID_JOYPAD_UP:     m = HidNpadButton_Up; break;
            case RETRO_DEVICE_ID_JOYPAD_DOWN:   m = HidNpadButton_Down; break;
            case RETRO_DEVICE_ID_JOYPAD_LEFT:   m = HidNpadButton_Left; break;
            case RETRO_DEVICE_ID_JOYPAD_RIGHT:  m = HidNpadButton_Right; break;
            case RETRO_DEVICE_ID_JOYPAD_START:  m = HidNpadButton_Plus; break;
            case RETRO_DEVICE_ID_JOYPAD_SELECT: m = HidNpadButton_Minus; break;
            case RETRO_DEVICE_ID_JOYPAD_L:      m = HidNpadButton_L; break;
            case RETRO_DEVICE_ID_JOYPAD_R:      m = HidNpadButton_R; break;
            case RETRO_DEVICE_ID_JOYPAD_L2:     m = HidNpadButton_ZL; break;
            case RETRO_DEVICE_ID_JOYPAD_R2:     m = HidNpadButton_ZR; break;
            case RETRO_DEVICE_ID_JOYPAD_L3:     m = HidNpadButton_StickL; break;
            case RETRO_DEVICE_ID_JOYPAD_R3:     m = HidNpadButton_StickR; break;
        }
        return (b & m) ? 1 : 0;
    }

    if (base == RETRO_DEVICE_ANALOG && index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
        HidAnalogStickState s = padGetStickPos(&p, index == RETRO_DEVICE_INDEX_ANALOG_LEFT ? 0 : 1);
        if (id == RETRO_DEVICE_ID_ANALOG_X) return (int16_t)s.x;
        if (id == RETRO_DEVICE_ID_ANALOG_Y) return (int16_t)(s.y == -32768 ? 32767 : -s.y);   // libretro: +Y hacia abajo
    }
    return 0;
}

void Input::rumble(unsigned port, bool strong, uint16_t strength) {
    if (port > 1 || !Config::get().isOn("rumble")) return;
    float v = strength / 65535.f;
    if (strong) vibLo_[port] = v; else vibHi_[port] = v;
    HidVibrationValue vals[2];
    for (auto& val : vals) {
        val.amp_low = vibLo_[port]; val.freq_low = 160.f;
        val.amp_high = vibHi_[port]; val.freq_high = 320.f;
    }
    hidSendVibrationValues(vib_[port], vals, 2);
}
