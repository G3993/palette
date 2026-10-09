#pragma once
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "sources/AudioFeatures.h"

namespace palette::live {

// Microphone (or default input) → FFT → the band matrix Palette and Easel
// share: Level / Bass / Mid / High, transient Hits per band, Presence
// (sustained energy), Time (clocks that advance with band energy), and a
// beat pulse. Fills Easel's AudioFeatures so a shader hears the same thing
// in both apps. Smoothing follows the house rule (0.85 default).
class Audio {
public:
    static constexpr int kFFT = 1024;
    bool start(int deviceIndex = -1);
    void stop();
    bool running() const { return m_running; }
    const std::string& error() const { return m_error; }

    // Call once per frame.
    void update(float dt);

    // 0..1 smoothed signals
    float level() const { return m_level; }
    float bass() const { return m_bass; }
    float mid() const { return m_mid; }
    float high() const { return m_high; }
    float beat() const { return m_beat; }
    float bassHit() const { return m_bassHit; }
    float midHit() const { return m_midHit; }
    float highHit() const { return m_highHit; }
    float bpm() const { return m_bpm; }
    const AudioFeatures& features() const { return m_features; }
    // Last 128 level samples for the Live waveform.
    const std::array<float, 128>& history() const { return m_history; }
    const std::vector<float>& spectrum() const { return m_spectrum; } // kFFT/2 magnitudes

    // Device list for the Source/Live sheets.
    static std::vector<std::string> devices();

    // miniaudio callback target
    void onCapture(const float* frames, unsigned count, unsigned channels);

private:
    void analyze();
    bool m_running = false;
    std::string m_error;
    void* m_device = nullptr; // ma_device*
    void* m_context = nullptr;
    std::mutex m_mx;
    std::vector<float> m_ring; size_t m_ringPos = 0;
    std::vector<float> m_window, m_re, m_im, m_spectrum, m_prevSpec;
    float m_level = 0, m_bass = 0, m_mid = 0, m_high = 0, m_beat = 0;
    float m_rawLevel = 0, m_rawBass = 0, m_rawMid = 0, m_rawHigh = 0;
    float m_bassEnv = 0, m_midEnv = 0, m_highEnv = 0, m_bassHit = 0, m_midHit = 0, m_highHit = 0;
    float m_bassPres = 0, m_midPres = 0, m_highPres = 0, m_levelPres = 0;
    float m_bassTime = 0, m_midTime = 0, m_highTime = 0, m_time = 0;
    float m_fluxAvg = 0, m_sinceBeat = 0, m_bpm = 120, m_beatPhase = 0;
    float m_agc = 1.0f;
    AudioFeatures m_features;
    std::array<float, 128> m_history{};
    int m_histPos = 0;
};

} // namespace palette::live
