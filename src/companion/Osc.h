#pragma once
#include <string>
#include <variant>
#include <vector>

namespace palette::companion {

// Minimal OSC 1.0 sender over UDP. Enough for Easel's verbs: strings, floats, ints.
class Osc {
public:
    using Arg = std::variant<int, float, std::string>;
    bool open(const std::string& host, int port);
    void close();
    bool isOpen() const { return m_sock >= 0; }
    bool send(const std::string& address, const std::vector<Arg>& args);
    ~Osc() { close(); }
private:
    int m_sock = -1;
    std::string m_host; int m_port = 0;
};

} // namespace palette::companion
