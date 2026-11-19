#pragma once
#include <string>
#include <vector>

// Red mínima: HTTPS con el servicio SSL del sistema (sin librerías externas).
namespace net {
bool init();                 // sockets + nifm + ssl
void shutdown();
bool online();               // conexión a internet activa
// GET https://host/path. Sigue hasta 4 redirecciones. Devuelve el código HTTP (0 = error de red).
int httpsGet(const std::string& url, std::vector<unsigned char>& body, bool verifyCerts = true);
std::string urlEncode(const std::string& s);
}
