#include "net.h"
#include "log.h"
#include <switch.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace net {

static bool s_sock = false, s_nifm = false, s_ssl = false;

bool init() {
    s_sock = R_SUCCEEDED(socketInitializeDefault());
    s_nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    s_ssl  = R_SUCCEEDED(sslInitialize(3));
    LOGI("[net] sockets=%d nifm=%d ssl=%d", s_sock, s_nifm, s_ssl);
    return s_sock && s_ssl;
}

void shutdown() {
    if (s_ssl) sslExit();
    if (s_nifm) nifmExit();
    if (s_sock) socketExit();
    s_ssl = s_nifm = s_sock = false;
}

bool online() {
    if (!s_nifm) return false;
    NifmInternetConnectionType type; u32 strength; NifmInternetConnectionStatus st;
    return R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &st)) && st == NifmInternetConnectionStatus_Connected;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

// Una petición; devuelve código HTTP y, si es redirección, la nueva URL en 'location'
static int getOnce(const std::string& host, const std::string& path, std::vector<unsigned char>& body,
                   std::string& location, bool verify) {
    body.clear(); location.clear();
    addrinfo hints = {}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), "443", &hints, &res) != 0 || !res) { LOGI("[net] DNS falló: %s", host.c_str()); return 0; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    timeval tv = {10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int c = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (c != 0) { close(fd); LOGI("[net] connect falló: %s", host.c_str()); return 0; }

    SslContext ctx; SslConnection conn;
    if (R_FAILED(sslCreateContext(&ctx, SslVersion_Auto))) { close(fd); return 0; }
    if (R_FAILED(sslContextCreateConnection(&ctx, &conn))) { sslContextClose(&ctx); close(fd); return 0; }
    int sslfd = socketSslConnectionSetSocketDescriptor(&conn, fd);
    if (sslfd == -1 && errno != ENOENT) { sslConnectionClose(&conn); sslContextClose(&ctx); close(fd); return 0; }
    sslConnectionSetHostName(&conn, host.c_str(), host.size() + 1);
    if (!verify) sslConnectionSetVerifyOption(&conn, 0);
    int status = 0;
    Result rc = sslConnectionDoHandshake(&conn, nullptr, nullptr, nullptr, 0);
    if (R_FAILED(rc)) {
        LOGI("[net] handshake TLS falló con %s (0x%x)", host.c_str(), rc);
    } else {
        std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                          "\r\nUser-Agent: PSX-K2/0.4\r\nAccept: */*\r\nConnection: close\r\n\r\n";
        u32 w = 0;
        sslConnectionWrite(&conn, req.data(), req.size(), &w);
        std::vector<unsigned char> raw;
        unsigned char buf[16384];
        for (;;) {
            u32 n = 0;
            if (R_FAILED(sslConnectionRead(&conn, buf, sizeof buf, &n)) || n == 0) break;
            raw.insert(raw.end(), buf, buf + n);
            if (raw.size() > 8 * 1024 * 1024) break;      // límite de seguridad
        }
        // Cabeceras
        std::string head;
        size_t hend = 0;
        for (size_t i = 0; i + 3 < raw.size(); i++)
            if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') { hend = i + 4; break; }
        if (hend) {
            head.assign((char*)raw.data(), hend);
            status = atoi(head.c_str() + head.find(' ') + 1);
            std::string lower = head;
            for (auto& ch : lower) ch = (char)tolower(ch);
            size_t lp = lower.find("\r\nlocation:");
            if (lp != std::string::npos) {
                size_t s = lp + 11; while (s < head.size() && head[s] == ' ') s++;
                location = head.substr(s, head.find("\r\n", s) - s);
            }
            bool chunked = lower.find("transfer-encoding: chunked") != std::string::npos;
            if (!chunked) body.assign(raw.begin() + hend, raw.end());
            else {
                size_t p = hend;
                while (p < raw.size()) {
                    size_t le = p; while (le + 1 < raw.size() && !(raw[le] == '\r' && raw[le + 1] == '\n')) le++;
                    unsigned long len = strtoul(std::string((char*)&raw[p], le - p).c_str(), nullptr, 16);
                    p = le + 2;
                    if (len == 0 || p + len > raw.size()) break;
                    body.insert(body.end(), raw.begin() + p, raw.begin() + p + len);
                    p += len + 2;
                }
            }
        }
    }
    sslConnectionClose(&conn);
    sslContextClose(&ctx);
    if (sslfd >= 0) close(sslfd);
    close(fd);
    return status;
}

int httpsGet(const std::string& url0, std::vector<unsigned char>& body, bool verify) {
    if (!s_sock || !s_ssl) return 0;
    std::string url = url0;
    for (int hop = 0; hop < 5; hop++) {
        if (url.compare(0, 8, "https://") != 0) return 0;
        size_t slash = url.find('/', 8);
        std::string host = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
        std::string path = slash == std::string::npos ? "/" : url.substr(slash);
        std::string loc;
        int st = getOnce(host, path, body, loc, verify);
        if ((st == 301 || st == 302 || st == 307 || st == 308) && !loc.empty()) {
            url = loc[0] == '/' ? "https://" + host + loc : loc;
            continue;
        }
        return st;
    }
    return 0;
}
}
