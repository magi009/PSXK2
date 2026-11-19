#pragma once
#include <string>
#include <vector>

class Video; class Audio; class Input;

// Puente con el núcleo libretro enlazado estáticamente (Beetle PSX HW).
class CoreHost {
public:
    CoreHost(Video& v, Audio& a, Input& i);
    void init();
    void deinit();

    bool loadGame(const std::string& path, std::string& error);
    void unloadGame();
    bool loaded() const { return loaded_; }
    const std::string& gameStem() const { return stem_; }

    void runFrame(bool fastForward);
    void reset();

    bool saveState(int slot);
    bool loadState(int slot);
    bool stateExists(int slot) const;
    void flushMemcard(bool force);

    // Multi-disco (.m3u / .pbp multidisco)
    unsigned discCount() const;
    unsigned discIndex() const;
    bool swapDisc(unsigned index);

    void applyControllers();
    bool takeScreenshot(std::string& outPath);
    float fps() const { return fps_; }

private:
    std::string statePath(int slot) const;
    Video& video_; Audio& audio_; Input& input_;
    bool loaded_ = false;
    std::string stem_, path_;
    std::vector<unsigned char> srmShadow_;
    unsigned long long lastSrmCheck_ = 0;
    float fps_ = 0; unsigned frames_ = 0; unsigned long long fpsTick_ = 0;
};
