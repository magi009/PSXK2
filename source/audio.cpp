#include "audio.h"
#include "log.h"
#include <switch.h>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

static AudioOutBuffer s_bufs[4];
static constexpr size_t kBufBytes = (1024 * 2 * sizeof(int16_t) + 0xFFF) & ~0xFFF;

bool Audio::init() {
    if (R_FAILED(audoutInitialize())) { LOGI("[audio] audoutInitialize falló"); return false; }
    if (R_FAILED(audoutStartAudioOut())) { LOGI("[audio] start falló"); return false; }
    for (int i = 0; i < kBufs; i++) {
        mem_[i] = (int16_t*)memalign(0x1000, kBufBytes);
        memset(mem_[i], 0, kBufBytes);
        s_bufs[i] = {};
        s_bufs[i].buffer = mem_[i];
        s_bufs[i].buffer_size = kBufBytes;
        s_bufs[i].data_size = kFrames * 2 * sizeof(int16_t);
    }
    ok_ = true;
    LOGI("[audio] %u Hz, %u canales", audoutGetSampleRate(), audoutGetChannelCount());
    return true;
}

void Audio::shutdown() {
    if (!ok_) return;
    audoutStopAudioOut();
    audoutExit();
    for (auto* m : mem_) free(m);
    ok_ = false;
}

void Audio::setInputRate(double hz) { baseStep_ = step_ = hz / 48000.0; }

float Audio::fill() const {
    int queued = 0;
    for (int i = 0; i < kBufs; i++) if (inUse_[i]) queued++;
    return (queued * (float)kFrames + fill_) / (kBufs * (float)kFrames);
}

// Control dinámico de frecuencia (como RetroArch): con VSync la imagen marca el ritmo (60 Hz) y el
// juego produce audio para 59,94 Hz. Se corrige la relación de remuestreo como máximo ±0,5%
// para mantener la cola a la mitad: ni cortes (cola vacía) ni retraso creciente (cola llena).
void Audio::updateRate() {
    if (block_) { step_ = baseStep_; return; }
    float f = fill();
    double dev = (f - 0.5f) / 0.5f;              // -1 (vacía) .. +1 (llena)
    if (dev > 1) dev = 1; if (dev < -1) dev = -1;
    step_ = baseStep_ * (1.0 + 0.005 * dev);
}

// Marca como libres los buffers que audout ya reprodujo
static bool reclaimOne(bool wait, bool* inUse) {
    AudioOutBuffer* rel = nullptr; u32 cnt = 0;
    Result rc = wait ? audoutWaitPlayFinish(&rel, &cnt, UINT64_MAX)
                     : audoutGetReleasedAudioOutBuffer(&rel, &cnt);
    if (R_FAILED(rc) || !rel || cnt == 0) return false;
    for (int i = 0; i < 4; i++) if (&s_bufs[i] == rel) inUse[i] = false;
    return true;
}

void Audio::submit() {
    s_bufs[cur_].data_size = fill_ * 2 * sizeof(int16_t);
    armDCacheFlush(mem_[cur_], kBufBytes);
    if (R_SUCCEEDED(audoutAppendAudioOutBuffer(&s_bufs[cur_]))) inUse_[cur_] = true;
    fill_ = 0;

    while (reclaimOne(false, inUse_)) {}
    for (;;) {
        for (int i = 1; i <= kBufs; i++) {
            int n = (cur_ + i) % kBufs;
            if (!inUse_[n]) { cur_ = n; return; }
        }
        // Sin buffers libres: con audio sync esperamos (esto marca el ritmo a ~60 fps);
        // sin audio sync (avance rápido) reutilizamos el actual y se pierde ese trozo de audio.
        if (!block_) { cur_ = -1; return; }
        reclaimOne(true, inUse_);
    }
}

bool Audio::acquire() {
    if (cur_ >= 0) return true;
    while (reclaimOne(false, inUse_)) {}
    for (int i = 0; i < kBufs; i++) if (!inUse_[i]) { cur_ = i; fill_ = 0; return true; }
    return false;
}

void Audio::push(const int16_t* in, size_t count, bool block) {
    if (!ok_) return;
    block_ = block;
    updateRate();
    for (size_t i = 0; i < count;) {
        // interpolación lineal entre la muestra anterior y la actual
        while (pos_ < 1.0) {
            if (acquire()) {
                int16_t l = (int16_t)(prevL_ + (in[i * 2] - prevL_) * pos_);
                int16_t r = (int16_t)(prevR_ + (in[i * 2 + 1] - prevR_) * pos_);
                mem_[cur_][fill_ * 2] = l;
                mem_[cur_][fill_ * 2 + 1] = r;
                if (++fill_ == kFrames) submit();
            }
            pos_ += step_;
        }
        pos_ -= 1.0;
        prevL_ = in[i * 2]; prevR_ = in[i * 2 + 1];
        i++;
    }
}

void Audio::flushSilence() { pos_ = 0; prevL_ = prevR_ = 0; fill_ = 0; }
