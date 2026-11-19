#pragma once
#include <cstddef>
#include <cstdint>

// Salida de audio con audout (48 kHz estéreo). El núcleo produce 44.1 kHz:
// se remuestrea con interpolación lineal. La espera por buffers libres
// sincroniza la emulación a la velocidad real (audio sync).
class Audio {
public:
    bool init();
    void shutdown();
    void setInputRate(double hz);
    // Ocupación de la cola (0..1); se usa para el control dinámico de frecuencia
    float fill() const;
    void push(const int16_t* frames, size_t count, bool block);
    void flushSilence();

private:
    void submit();
    bool acquire();
    static constexpr int kBufs = 4;
    static constexpr int kFrames = 1024;      // ~21 ms por buffer
    int16_t* mem_[kBufs] = {};
    bool inUse_[kBufs] = {};
    int cur_ = 0;
    size_t fill_ = 0;                         // frames en el buffer actual
    double baseStep_ = 44100.0 / 48000.0, step_ = 44100.0 / 48000.0, pos_ = 0.0;
    void updateRate();
    int16_t prevL_ = 0, prevR_ = 0;
    bool ok_ = false;
    bool block_ = true;
};
