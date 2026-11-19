#pragma once
#include <string>
#include <vector>

// Estructura en la microSD:
//   sdmc:/switch/psx-k2/
//     psx-k2.nro
//     bios/            scph5500.bin (JP) scph5501.bin (US) scph5502.bin (EU)
//     roms/            .cue/.bin .chd .pbp .m3u .iso .ccd .exe (subcarpetas permitidas)
//     saves/memcards/  tarjetas de memoria (.srm ranura 1, .mcr ranura 2)
//     saves/states/    estados rápidos (10 ranuras por juego)
//     screenshots/     capturas .bmp
//     cheats/          trucos .cht
//     config/          psx-k2.ini (global)  games/<juego>.ini (por juego)
//     logs/            psx-k2.log
namespace paths {
extern std::string root, bios, roms, memcards, states, screenshots, cheats, config, gameConfigs, logs;

void init();
bool exists(const std::string& p);
std::string stem(const std::string& path);          // "roms/RE2 (Disc 1).cue" -> "RE2 (Disc 1)"
std::string ext(const std::string& path);           // en minúsculas, con punto
std::vector<std::string> scanRoms();                // rutas completas, ordenadas
std::vector<std::string> missingBios();             // lista vacía si hay al menos una válida
bool readFile(const std::string& p, std::vector<unsigned char>& out);
bool writeFile(const std::string& p, const void* data, size_t size);
}
