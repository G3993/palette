#include "companion/Osc.h"
#include <cstring>
#include <cstdint>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace palette::companion {

static void pad(std::vector<uint8_t>& b) { while (b.size() % 4) b.push_back(0); }
static void putStr(std::vector<uint8_t>& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); pad(b); }
static void putBE32(std::vector<uint8_t>& b, uint32_t v) { b.push_back(v >> 24); b.push_back(v >> 16); b.push_back(v >> 8); b.push_back(v); }

bool Osc::open(const std::string& host, int port) {
    close();
#ifdef _WIN32
    static bool wsa = false; if (!wsa) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); wsa = true; }
#endif
    m_sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sock < 0) return false;
    m_host = host; m_port = port;
    return true;
}

void Osc::close() {
    if (m_sock >= 0) {
#ifdef _WIN32
        closesocket(m_sock);
#else
        ::close(m_sock);
#endif
        m_sock = -1;
    }
}

bool Osc::send(const std::string& address, const std::vector<Arg>& args) {
    if (m_sock < 0) return false;
    std::vector<uint8_t> b;
    putStr(b, address);
    std::string tags = ",";
    for (auto& a : args) tags += std::holds_alternative<int>(a) ? 'i' : std::holds_alternative<float>(a) ? 'f' : 's';
    putStr(b, tags);
    for (auto& a : args) {
        if (auto* i = std::get_if<int>(&a)) putBE32(b, (uint32_t)*i);
        else if (auto* f = std::get_if<float>(&a)) { uint32_t u; std::memcpy(&u, f, 4); putBE32(b, u); }
        else putStr(b, std::get<std::string>(a));
    }
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)m_port);
    inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);
    return sendto(m_sock, (const char*)b.data(), (int)b.size(), 0, (sockaddr*)&addr, sizeof addr) == (int)b.size();
}

} // namespace palette::companion
