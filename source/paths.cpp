#include "paths.h"
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace paths {
std::string root = "sdmc:/switch/psx-k2";
std::string bios, roms, memcards, states, screenshots, cheats, config, gameConfigs, logs;

static void mk(const std::string& p) { mkdir(p.c_str(), 0777); }

void init() {
    bios        = root + "/bios";
    roms        = root + "/roms";
    memcards    = root + "/saves/memcards";
    states      = root + "/saves/states";
    screenshots = root + "/screenshots";
    cheats      = root + "/cheats";
    config      = root + "/config";
    gameConfigs = root + "/config/games";
    logs        = root + "/logs";
    mk(root); mk(bios); mk(roms); mk(root + "/saves"); mk(memcards); mk(states);
    mk(screenshots); mk(cheats); mk(config); mk(gameConfigs); mk(logs);
}

bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

std::string stem(const std::string& path) {
    size_t s = path.find_last_of('/');
    std::string f = (s == std::string::npos) ? path : path.substr(s + 1);
    size_t d = f.find_last_of('.');
    return d == std::string::npos ? f : f.substr(0, d);
}

std::string ext(const std::string& path) {
    size_t d = path.find_last_of('.');
    if (d == std::string::npos) return "";
    std::string e = path.substr(d);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e;
}

static const char* kExts[] = {".cue", ".chd", ".pbp", ".m3u", ".iso", ".ccd", ".exe", ".bin"};

static void scanDir(const std::string& dir, int depth, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> files;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;
        if (e->d_type == DT_DIR) { if (depth < 2) scanDir(full, depth + 1, out); continue; }
        files.push_back(full);
    }
    closedir(d);
    for (auto& f : files) {
        std::string e = ext(f);
        bool ok = false;
        for (auto* k : kExts) if (e == k) ok = true;
        if (!ok) continue;
        // .bin sólo si no hay un .cue con el mismo nombre (evita duplicados)
        if (e == ".bin") {
            std::string base = f.substr(0, f.size() - 4);
            if (exists(base + ".cue") || exists(base + ".CUE")) continue;
        }
        // Si existe un .m3u en la carpeta, ocultamos los discos individuales listados en él
        out.push_back(f);
    }
}

std::vector<std::string> scanRoms() {
    std::vector<std::string> out;
    scanDir(roms, 0, out);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return stem(a) < stem(b);
    });
    return out;
}

std::vector<std::string> missingBios() {
    static const char* kBios[] = {"scph5500.bin", "scph5501.bin", "scph5502.bin"};
    std::vector<std::string> missing;
    bool any = false;
    for (auto* b : kBios) {
        if (exists(bios + "/" + b)) any = true; else missing.push_back(b);
    }
    if (any) missing.clear();
    return missing;
}

bool readFile(const std::string& p, std::vector<unsigned char>& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    bool ok = n <= 0 || fread(out.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

bool writeFile(const std::string& p, const void* data, size_t size) {
    std::string tmp = p + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(p.c_str());
    return rename(tmp.c_str(), p.c_str()) == 0;   // escritura atómica: no corrompe memcards
}
}
