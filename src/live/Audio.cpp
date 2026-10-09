#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#include "miniaudio.h"
#include "live/Audio.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace palette::live {

static void maCallback(ma_device* dev, void*, const void* input, ma_uint32 frames) {
    auto* self = static_cast<Audio*>(dev->pUserData);
    if (self && input) self->onCapture(static_cast<const float*>(input), frames, dev->capture.channels);
}

std::vector<std::string> Audio::devices() {
    std::vector<std::string> out;
    ma_context ctx;
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) return out;
    ma_device_info* caps = nullptr; ma_uint32 n = 0;
    if (ma_context_get_devices(&ctx, nullptr, nullptr, &caps, &n) == MA_SUCCESS)
        for (ma_uint32 i = 0; i < n; ++i) out.emplace_back(caps[i].name);
    ma_context_uninit(&ctx);
    return out;
}

bool Audio::start(int deviceIndex) {
    stop();
    auto* ctx = new ma_context();
    if (ma_context_init(nullptr, 0, nullptr, ctx) != MA_SUCCESS) { m_error = "audio context failed"; delete ctx; return false; }
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate = 48000;
    cfg.dataCallback = maCallback;
    cfg.pUserData = this;
    if (deviceIndex >= 0) {
        ma_device_info* caps = nullptr; ma_uint32 n = 0;
        if (ma_context_get_devices(ctx, nullptr, nullptr, &caps, &n) == MA_SUCCESS && (ma_uint32)deviceIndex < n)
            cfg.capture.pDeviceID = &caps[deviceIndex].id;
    }
    auto* dev = new ma_device();
    if (ma_device_init(ctx, &cfg, dev) != MA_SUCCESS) { m_error = "no input device"; delete dev; ma_context_uninit(ctx); delete ctx; return false; }
    if (ma_device_start(dev) != MA_SUCCESS) { m_error = "input device would not start"; ma_device_uninit(dev); delete dev; ma_context_uninit(ctx); delete ctx; return false; }
    m_device = dev; m_context = ctx;
    m_ring.assign(kFFT * 4, 0.0f); m_ringPos = 0;
    m_window.resize(kFFT); for (int i = 0; i < kFFT; ++i) m_window[i] = 0.5f * (1.0f - std::cos(6.2831853f * i / (kFFT - 1)));
    m_re.assign(kFFT, 0); m_im.assign(kFFT, 0); m_spectrum.assign(kFFT / 2, 0); m_prevSpec.assign(kFFT / 2, 0);
    m_running = true; m_error.clear();
    return true;
}

void Audio::stop() {
    if (m_device) { ma_device_uninit((ma_device*)m_device); delete (ma_device*)m_device; m_device = nullptr; }
    if (m_context) { ma_context_uninit((ma_context*)m_context); delete (ma_context*)m_context; m_context = nullptr; }
    m_running = false;
}

void Audio::onCapture(const float* frames, unsigned count, unsigned channels) {
    std::lock_guard<std::mutex> lk(m_mx);
    for (unsigned i = 0; i < count; ++i) {
        float s = 0; for (unsigned c = 0; c < channels; ++c) s += frames[i * channels + c];
        s /= (float)channels;
        m_ring[m_ringPos] = s; m_ringPos = (m_ringPos + 1) % m_ring.size();
    }
}

// In-place iterative radix-2 FFT.
static void fft(std::vector<float>& re, std::vector<float>& im) {
    int n = (int)re.size();
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -6.2831853f / len, wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; ++k) {
                float ur = re[i + k], ui = im[i + k];
                float vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                float vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

void Audio::analyze() {
    {
        std::lock_guard<std::mutex> lk(m_mx);
        size_t n = m_ring.size();
        for (int i = 0; i < kFFT; ++i) {
            size_t idx = (m_ringPos + n - kFFT + i) % n;
            m_re[i] = m_ring[idx] * m_window[i]; m_im[i] = 0;
        }
    }
    float rms = 0; for (int i = 0; i < kFFT; ++i) rms += m_re[i] * m_re[i];
    rms = std::sqrt(rms / kFFT) * 2.0f;
    fft(m_re, m_im);
    const float binHz = 48000.0f / kFFT;
    float bass = 0, mid = 0, high = 0, flux = 0;
    for (int i = 1; i < kFFT / 2; ++i) {
        float m = std::sqrt(m_re[i] * m_re[i] + m_im[i] * m_im[i]) / (kFFT / 4);
        m_spectrum[i] = m;
        float f = i * binHz;
        if (f >= 40 && f < 250) bass += m;
        else if (f >= 250 && f < 2000) mid += m;
        else if (f >= 2000 && f < 9000) high += m;
        float d = m - m_prevSpec[i]; if (d > 0 && f < 300) flux += d;
        m_prevSpec[i] = m;
    }
    // Simple AGC so quiet rooms still move. Tracks the slow peak of the level.
    float raw = rms * m_agc;
    if (raw > 1.0f) m_agc *= 0.98f; else if (raw < 0.35f && rms > 0.002f) m_agc *= 1.002f;
    m_agc = std::clamp(m_agc, 1.0f, 40.0f);
    m_rawLevel = std::clamp(raw, 0.0f, 1.0f);
    m_rawBass = std::clamp(bass * 0.9f * m_agc, 0.0f, 1.0f);
    m_rawMid = std::clamp(mid * 0.5f * m_agc, 0.0f, 1.0f);
    m_rawHigh = std::clamp(high * 0.9f * m_agc, 0.0f, 1.0f);
    // onset / beat from bass spectral flux with adaptive threshold
    m_fluxAvg = m_fluxAvg * 0.95f + flux * 0.05f;
    if (flux > m_fluxAvg * 1.8f + 0.01f && m_sinceBeat > 0.25f) {
        if (m_sinceBeat < 2.0f) { float b = 60.0f / m_sinceBeat; m_bpm = m_bpm * 0.8f + std::clamp(b, 60.0f, 180.0f) * 0.2f; }
        m_sinceBeat = 0; m_beat = 1.0f;
    }
}

void Audio::update(float dt) {
    if (!m_running) return;
    analyze();
    auto follow = [&](float& s, float raw, float up, float down) { float a = raw > s ? up : down; s += (raw - s) * std::min(1.0f, a * dt); };
    follow(m_level, m_rawLevel, 18, 6); follow(m_bass, m_rawBass, 18, 6); follow(m_mid, m_rawMid, 18, 6); follow(m_high, m_rawHigh, 18, 6);
    // hits: fast envelope minus slow envelope
    auto hit = [&](float& env, float& out, float raw) { env += (raw - env) * std::min(1.0f, 2.5f * dt); out = std::clamp((raw - env) * 3.0f, 0.0f, 1.0f); };
    hit(m_bassEnv, m_bassHit, m_rawBass); hit(m_midEnv, m_midHit, m_rawMid); hit(m_highEnv, m_highHit, m_rawHigh);
    follow(m_bassPres, m_rawBass, 1.2f, 0.6f); follow(m_midPres, m_rawMid, 1.2f, 0.6f); follow(m_highPres, m_rawHigh, 1.2f, 0.6f); follow(m_levelPres, m_rawLevel, 1.2f, 0.6f);
    m_bassTime += m_bass * dt; m_midTime += m_mid * dt; m_highTime += m_high * dt; m_time += m_level * dt;
    m_sinceBeat += dt; m_beat = std::max(0.0f, m_beat - dt * 3.5f);
    float beatLen = 60.0f / std::max(40.0f, m_bpm);
    m_beatPhase = std::fmod(m_sinceBeat, beatLen) / beatLen;

    AudioFeatures& f = m_features;
    f.level = m_level; f.bass = m_bass; f.sub = m_bass * 0.7f; f.lowMid = m_mid * 0.8f; f.highMid = m_mid; f.treble = m_high;
    f.punch = std::max(m_bassHit, m_midHit); f.beat = m_beat; f.beatPhase = m_beatPhase; f.beatPulse = m_beat; f.barPhase = std::fmod(m_sinceBeat, beatLen * 4) / (beatLen * 4);
    f.bpm = m_bpm;
    m_history[m_histPos] = m_level; m_histPos = (m_histPos + 1) % (int)m_history.size();
}

} // namespace palette::live
