#pragma once
#include <functional>
#include <string>

namespace palette::live {

// Captures what the computer is playing (macOS 14.2+ Core Audio process tap).
// Delivers interleaved float frames on the Core Audio thread. On other
// platforms start() returns false and Audio falls back to miniaudio loopback
// (Windows) or the microphone.
class SystemTap {
public:
    using Sink = std::function<void(const float* frames, unsigned count, unsigned channels)>;
    bool start(Sink sink);
    void stop();
    bool running() const { return m_running; }
    double sampleRate() const { return m_rate; }
    const std::string& error() const { return m_error; }
    static bool available();
    Sink sink;   // public for the IOProc
private:
    bool m_running = false; double m_rate = 48000; std::string m_error;
    unsigned m_tapID = 0, m_aggID = 0; void* m_proc = nullptr;
};

} // namespace palette::live
