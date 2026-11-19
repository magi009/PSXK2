#include "input.h"
#include "config.h"
#include <libretro.h>
#include <cstring>
#include "log.h"

void Input::init() {
    // Switch 2: los Joy-Con 2 (y el Pro Controller 2) llegan como NpadSystemExt a apps que no declaran
    // sus estilos propios; libounce los lee por esa vía. Sin este bit no hay mandos en absoluto.
    padConfigureInput(2, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadSystemExt);
    padInitialize(&pads_[0], HidNpadIdType_No1, HidNpadIdType_Handheld);
    padInitialize(&pads_[1], HidNpadIdType_No2);
    hidInitializeVibrationDevices(vib_[0], 2, HidNpadIdType_No1, (HidNpadStyleTag)(HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadJoyDual));
    hidInitializeVibrationDevices(vib_[1], 2, HidNpadIdType_No2, (HidNpadStyleTag)(HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadJoyDual));
}

void Input::update() {
    padUpdate(&pads_[0]);
    padUpdate(&pads_[1]);
    // Registro: estilo de cada mando cuando cambia (para diagnosticar mandos no detectados)
    for (int i = 0; i < 2; i++) {
        u32 st = padGetStyleSet(&pads_[i]);
        if (st != lastStyle_[i]) {
            LOGI("[input] jugador %d: estilo de mando 0x%x%s", i + 1, st, padIsConnected(&pads_[i]) ? " (conectado)" : " (sin mando)");
            lastStyle_[i] = st;
        }
    }
    u64 h = padGetButtons(&pads_[0]);
    u64 d = padGetButtonsDown(&pads_[0]);
    // Se ignoran los bits de dirección de los sticks (0xFF0000), que llenaban el registro
    u64 hb = h & ~0xFF0000ULL;
    if (hb != lastButtons_ && buttonLogs_ < 300) { buttonLogs_++; LOGI("[input] botones j1 = 0x%llx", (unsigned long long)hb); lastButtons_ = hb; }
    probeRaw();
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
    // Salida de emergencia: − y + mantenidos 2 segundos
    if ((h & HidNpadButton_Minus) && (h & HidNpadButton_Plus)) {
        u64 now = armGetSystemTick();
        if (!comboStart_) comboStart_ = now;
        else if (armTicksToNs(now - comboStart_) >= 2000000000ULL) quit_ = true;
    } else {
        comboStart_ = 0;
    }
}

// Lee directamente cada canal de HID (una vez por segundo, los primeros 15 s) para ver por dónde
// llegan de verdad las pulsaciones en Switch 2
void Input::probeRaw() {
    u64 now = armGetSystemTick();
    if (probeLogs_ >= 15 || (lastProbe_ && armTicksToNs(now - lastProbe_) < 1000000000ULL)) return;
    lastProbe_ = now; probeLogs_++;
    HidNpadCommonState st;
    auto show = [&](const char* name, size_t n) {
        if (n) LOGI("[input]   %-18s attr=0x%x botones=0x%llx stickL=%d,%d", name, st.attributes,
                    (unsigned long long)st.buttons, st.analog_stick_l.x, st.analog_stick_l.y);
        else LOGI("[input]   %-18s (sin datos)", name);
    };
    LOGI("[input] sondeo %u: estilos handheld=0x%x no1=0x%x", probeLogs_,
         hidGetNpadStyleSet(HidNpadIdType_Handheld), hidGetNpadStyleSet(HidNpadIdType_No1));
    show("handheld/Handheld", hidGetNpadStatesHandheld(HidNpadIdType_Handheld, &st, 1));
    show("handheld/SystemExt", hidGetNpadStatesSystemExt(HidNpadIdType_Handheld, &st, 1));
    show("no1/FullKey", hidGetNpadStatesFullKey(HidNpadIdType_No1, &st, 1));
    show("no1/JoyDual", hidGetNpadStatesJoyDual(HidNpadIdType_No1, &st, 1));
    show("no1/SystemExt", hidGetNpadStatesSystemExt(HidNpadIdType_No1, &st, 1));
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
        int16_t r = (b & m) ? 1 : 0;
        static unsigned coreLogs = 0;
        if (r && coreLogs < 60) { coreLogs++; LOGI("[input] el núcleo lee puerto %u botón libretro %u = pulsado", port, id); }
        return r;
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
