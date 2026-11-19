#pragma once
#include <cstdint>
#include <map>
#include <string>

class Video;

// Portadas al estilo DuckStation/PCSX2:
//  - Se buscan en sdmc:/switch/psx-k2/covers/<juego>.png|.jpg (puedes poner las tuyas desde el PC).
//  - Si no hay y hay internet, se descargan solas (libretro-thumbnails, por nombre Redump).
//  - Se pueden cambiar desde la app: Caja, Pantalla de título o Captura.
// Todo el trabajo de red/decodificación va en un hilo aparte: el menú nunca se congela.
namespace covers {

enum class Kind { Box = 0, Title = 1, Snap = 2 };
enum class State { None, Loading, Ready, Missing };

void init(Video& video);
void shutdown();
// Llamar una vez por frame en el hilo principal: sube a la GPU como mucho 2 imágenes
void pump();

// Textura de la portada (0 si no hay todavía). Pide cargarla/descargarla si hace falta.
uint64_t get(const std::string& stem, float* aspect, State* state = nullptr);
// Cambiar portada: descarga otra variante y la guarda (sobrescribe la actual)
void replace(const std::string& stem, Kind kind);
// Quitar la portada (borra el archivo); no se volverá a descargar sola
void remove(const std::string& stem);
// Volver a intentar las que fallaron (p. ej. al conectarse a internet)
void retryMissing();
std::string folder();
std::string lastMessage();
}
