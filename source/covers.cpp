#include "covers.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "paths.h"
#include "video.h"
#include <condition_variable>
#include <cstring>
#include <sys/stat.h>
#include <cstdio>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace covers {

struct Job { std::string stem; bool download; Kind kind; bool force; };
struct Decoded { std::string stem; std::vector<uint8_t> rgba; int w = 0, h = 0; bool ok = false; };
struct Entry { uint64_t tex = 0; float aspect = 1.f; State state = State::None; unsigned gen = 0; };

static Video* s_video;
static std::thread s_thread;
static std::mutex s_mx;
static std::condition_variable s_cv;
static std::deque<Job> s_jobs;
static std::deque<Decoded> s_done;
static std::map<std::string, Entry> s_entries;
static std::set<std::string> s_noAuto;      // quitadas por el usuario: no descargar solas
static bool s_quit = false;
static std::string s_msg;
static std::string s_dir;

std::string folder() { return s_dir; }
std::string lastMessage() { std::lock_guard<std::mutex> l(s_mx); return s_msg; }
static void setMsg(const std::string& m) { std::lock_guard<std::mutex> l(s_mx); s_msg = m; }

// Nombre al estilo libretro-thumbnails: estos caracteres se sustituyen por "_"
static std::string thumbName(std::string n) {
    for (auto& c : n) if (strchr("&*/:`<>?\\|\"", c)) c = '_';
    return n;
}
// "Juego (USA) (Disc 1)" -> "Juego (USA)"
static std::string stripDisc(const std::string& n) {
    size_t p = n.find(" (Disc ");
    if (p == std::string::npos) return n;
    size_t e = n.find(')', p);
    return n.substr(0, p) + (e == std::string::npos ? "" : n.substr(e + 1));
}

static std::string localFile(const std::string& stem) {
    for (const char* e : {".png", ".jpg", ".jpeg", ".PNG", ".JPG"}) {
        std::string p = s_dir + "/" + stem + e;
        if (paths::exists(p)) return p;
    }
    return "";
}

// Decodifica y reduce (máx. 384 px de alto) para ahorrar memoria de vídeo
static bool decode(const std::vector<unsigned char>& file, Decoded& out) {
    int w, h, n;
    unsigned char* px = stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &n, 4);
    if (!px) return false;
    int tw = w, th = h;
    while (th > 384) { tw /= 2; th /= 2; }
    out.w = tw; out.h = th;
    out.rgba.resize((size_t)tw * th * 4);
    int fx = w / tw, fy = h / th;
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            unsigned acc[4] = {};
            for (int j = 0; j < fy; j++)
                for (int i = 0; i < fx; i++) {
                    const unsigned char* s = px + ((size_t)(y * fy + j) * w + (x * fx + i)) * 4;
                    for (int k = 0; k < 4; k++) acc[k] += s[k];
                }
            for (int k = 0; k < 4; k++) out.rgba[((size_t)y * tw + x) * 4 + k] = (uint8_t)(acc[k] / (fx * fy));
        }
    stbi_image_free(px);
    return true;
}

static bool download(const std::string& stem, Kind kind, std::vector<unsigned char>& file) {
    static const char* kFolder[] = {"Named_Boxarts", "Named_Titles", "Named_Snaps"};
    bool verify = Config::get().isOn("covers_verify");
    std::vector<std::string> names = {thumbName(stem)};
    std::string nd = thumbName(stripDisc(stem));
    if (nd != names[0]) names.push_back(nd);
    for (auto& n : names) {
        std::string url = "https://raw.githubusercontent.com/libretro-thumbnails/Sony_-_PlayStation/master/" +
                          std::string(kFolder[(int)kind]) + "/" + net::urlEncode(n) + ".png";
        int st = net::httpsGet(url, file, verify);
        LOGI("[covers] %s -> HTTP %d (%zu bytes)", n.c_str(), st, file.size());
        if (st == 200 && file.size() > 100) return true;
        if (st == 0) { setMsg("Sin conexión con el servidor de portadas"); return false; }
    }
    return false;
}

static void worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> l(s_mx);
            s_cv.wait(l, [] { return s_quit || !s_jobs.empty(); });
            if (s_quit) return;
            job = s_jobs.front(); s_jobs.pop_front();
        }
        Decoded d; d.stem = job.stem;
        std::vector<unsigned char> file;
        std::string local = job.force ? "" : localFile(job.stem);
        if (!local.empty() && paths::readFile(local, file)) {
            d.ok = decode(file, d);
            if (!d.ok) LOGI("[covers] no se pudo leer %s", local.c_str());
        } else if (job.download && net::online()) {
            if (download(job.stem, job.kind, file) && (d.ok = decode(file, d))) {
                // Si se reemplaza, borra variantes .jpg previas para que mande la nueva .png
                for (const char* e : {".jpg", ".jpeg", ".JPG"}) ::remove((s_dir + "/" + job.stem + e).c_str());
                paths::writeFile(s_dir + "/" + job.stem + ".png", file.data(), file.size());
                setMsg("Portada descargada: " + job.stem);
            } else if (job.force) setMsg("No hay esa imagen para " + job.stem);
        }
        std::lock_guard<std::mutex> l(s_mx);
        s_done.push_back(std::move(d));
    }
}

void init(Video& video) {
    s_video = &video;
    s_dir = paths::root + "/covers";
    mkdir(s_dir.c_str(), 0777);
    // Lista de portadas que el usuario quitó a propósito
    FILE* f = fopen((s_dir + "/.sin_portada.txt").c_str(), "r");
    if (f) { char line[512]; while (fgets(line, sizeof line, f)) { std::string s = line; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back(); if (!s.empty()) s_noAuto.insert(s); } fclose(f); }
    s_quit = false;
    s_thread = std::thread(worker);
}

void shutdown() {
    { std::lock_guard<std::mutex> l(s_mx); s_quit = true; }
    s_cv.notify_all();
    if (s_thread.joinable()) s_thread.join();
    for (auto& e : s_entries) if (e.second.tex && e.second.gen == s_video->textureGeneration()) s_video->destroyTexture(e.second.tex);
    s_entries.clear();
}

static void enqueue(const std::string& stem, bool download, Kind kind, bool force) {
    { std::lock_guard<std::mutex> l(s_mx); s_jobs.push_back({stem, download, kind, force}); }
    s_cv.notify_one();
}

void pump() {
    unsigned gen = s_video->textureGeneration();
    int budget = 1;   // una subida por frame: sin tirones al hacer scroll por la biblioteca
    while (budget > 0) {
        Decoded d;
        {
            std::lock_guard<std::mutex> l(s_mx);
            if (s_done.empty()) break;
            d = std::move(s_done.front()); s_done.pop_front();
        }
        Entry& e = s_entries[d.stem];
        if (e.tex && e.gen == gen) s_video->destroyTexture(e.tex);
        e.tex = 0;
        if (d.ok) {
            e.tex = s_video->createTexture(d.rgba.data(), d.w, d.h);
            e.aspect = (float)d.w / d.h; e.gen = gen;
            e.state = e.tex ? State::Ready : State::Missing;
            budget--;
        } else {
            e.state = State::Missing;
        }
    }
}

uint64_t get(const std::string& stem, float* aspect, State* state) {
    Entry& e = s_entries[stem];
    // El dispositivo de vídeo cambió (se cargó/cerró un juego): hay que volver a subirla
    if (e.state == State::Ready && e.gen != s_video->textureGeneration()) { e.tex = 0; e.state = State::None; }
    if (e.state == State::None) {
        e.state = State::Loading;
        bool autoDl = Config::get().isOn("covers_auto") && !s_noAuto.count(stem);
        enqueue(stem, autoDl, Kind::Box, false);
    }
    if (aspect) *aspect = e.aspect;
    if (state) *state = e.state;
    return e.state == State::Ready ? e.tex : 0;
}

static void saveNoAuto() {
    FILE* f = fopen((s_dir + "/.sin_portada.txt").c_str(), "w");
    if (!f) return;
    for (auto& s : s_noAuto) fprintf(f, "%s\n", s.c_str());
    fclose(f);
}

void replace(const std::string& stem, Kind kind) {
    s_noAuto.erase(stem); saveNoAuto();
    setMsg("Descargando portada...");
    s_entries[stem].state = State::Loading;
    enqueue(stem, true, kind, true);
}

void remove(const std::string& stem) {
    for (const char* e : {".png", ".jpg", ".jpeg", ".PNG", ".JPG"}) ::remove((s_dir + "/" + stem + e).c_str());
    s_noAuto.insert(stem); saveNoAuto();
    Entry& e = s_entries[stem];
    if (e.tex && e.gen == s_video->textureGeneration()) s_video->destroyTexture(e.tex);
    e = Entry(); e.state = State::Missing;
}

void retryMissing() {
    for (auto& e : s_entries)
        if (e.second.state == State::Missing && !s_noAuto.count(e.first)) e.second.state = State::None;
}
}
