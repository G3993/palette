#pragma once
// ─────────────────────────────────────────────────────────────────────────
// EaselAudio v1 — the shared audio-conditioning core.
//
// One reusable vocabulary used by the analyzer bands AND every binding
// (per-param shader bindings, layer-transform bindings, per-effect mods),
// per docs/EASELAUDIO_SPEC.md + spec/easel_audio_bus.json in ShaderClaw3:
//
//   Conditioner  gate → attack(sec,pow) → hold → release(sec,pow)
//                → hardChangeThreshold → character → remap → micro-slew
//   DbAGC        dB-domain auto gain (noise floor −60 dB, fast-grow /
//                slow-shrink range — UE/Synesthesia pattern)
//   HitDetector  dual fast/slow follower crossing (self-normalizing,
//                Ableton AnalysisGrabber) + AD envelope 10 ms / 350 ms
//                with per-edge shape powers 1.0 / 1.6
//   PresenceEnv  slow macro envelope (attack 1.5 s / release 4 s)
//   TempoTracker autocorrelation tempo over the onset-strength envelope
//                (50–220 BPM, octave-corrected, with confidence)
//
// All smoothing times are SECONDS, coefficients derived from dt every frame
// (k = 1 − exp(−dt/tau)). No per-frame alphas, ever.
//
// Smooth-by-default mandate: every default in this file is chosen so that
// legacy call sites reproduce their previous felt behavior exactly
// (character 0 = neutral, gate off, hold 0, hard-change bypass off, pow 1).
// ─────────────────────────────────────────────────────────────────────────
#include <cmath>
#include <algorithm>

namespace easelaudio {

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// dt-derived one-pole coefficient for a tau in seconds.
inline float k1(float dt, float tau) {
    return 1.0f - std::exp(-dt / std::max(tau, 1e-4f));
}

// ── Conditioning chain ───────────────────────────────────────────────────
struct ConditionerParams {
    // Gate (on the incoming 0..1 signal, threshold expressed in dBFS-ish of
    // the normalized value). Default −120 dB = OFF so legacy paths that never
    // had a gate keep behaving identically. Schema default for analyzer
    // bands is −60 dB (the AGC noise floor doubles as that gate).
    float gateDb = -120.0f;
    float gateHysteresisDb = 3.0f;

    // Attack / release as tau seconds (time constants of the exponential
    // approach), with per-edge curve-shape exponents (Notch "Shape Power";
    // 1.0 = pure exponential = legacy behavior).
    float attackSec = 0.01f;
    float attackPow = 1.0f;
    float holdSec = 0.0f;          // peak-hold before release (Resolume Fall)
    float releaseSec = 0.5f;
    float releasePow = 1.0f;

    // Deltas larger than this bypass smoothing entirely (Notch Hard Change
    // Threshold — "smooth but never mushy"). Default OFF (1e9) so existing
    // bindings keep their exact glide; the schema's 0.35 is applied by the
    // chopped side of the Character macro, never by default.
    float hardChangeThreshold = 1.0e9f;

    // −1 = extra One-Euro-ish smooth … +1 = spiky / derivative-boosted.
    // 0 = neutral = today's feel (legacy paths must pass 0).
    float character = 0.0f;

    // Remap (Math-CHOP Range + gamma). Identity by default.
    float inLo = 0.0f, inHi = 1.0f;
    float outLo = 0.0f, outHi = 1.0f;
    float gamma = 1.0f;

    // Mandatory micro-slew zipper guard (M4L pattern) — non-removable.
    // 3 ms tau: invisible at display frame rates, kills sub-ms zippers.
    float microSlewSec = 0.003f;

    // Role presets (UE per-purpose A/R defaults; schema rolePresets).
    static ConditionerParams general()       { ConditionerParams p; p.attackSec = 0.01f; p.releaseSec = 0.10f; return p; }
    static ConditionerParams visualBinding() { ConditionerParams p; p.attackSec = 0.01f; p.releaseSec = 0.50f; return p; }
    static ConditionerParams meter()         { ConditionerParams p; p.attackSec = 0.30f; p.releaseSec = 0.30f; return p; }
    static ConditionerParams stem()          { ConditionerParams p; p.attackSec = 0.01f; p.releaseSec = 0.75f; return p; }

    // Legacy-rate helper: existing code expresses smoothing as rates (1/s).
    void setRates(float attackRate, float releaseRate) {
        attackSec  = 1.0f / std::max(attackRate,  1e-3f);
        releaseSec = 1.0f / std::max(releaseRate, 1e-3f);
    }
};

struct Conditioner {
    ConditionerParams p;

    // state
    float env = 0.0f;       // post attack/hold/release
    float out = 0.0f;       // post remap + micro-slew
    float holdT = 0.0f;
    float prevIn = 0.0f;
    bool  gateOpen = false;
    bool  primed = false;

    void reset() { env = out = holdT = prevIn = 0.0f; gateOpen = false; primed = false; }

    // Feed one 0..1-ish sample; returns the conditioned value (remapped).
    float process(float x, float dt) {
        if (!(dt > 0.0f)) dt = 1.0f / 60.0f;
        if (dt > 0.1f)    dt = 0.1f;

        // 1. gate (hysteresis, threshold in dB of the normalized signal)
        if (p.gateDb > -119.0f) {
            float th = p.gateDb + (gateOpen ? -0.5f : 0.5f) * p.gateHysteresisDb;
            float lin = std::pow(10.0f, th / 20.0f);
            gateOpen = x >= lin;
            if (!gateOpen) x = 0.0f;
        }

        if (!primed) {
            // First sample: adopt it (avoids a 0→value ramp-in, matching the
            // legacy AudioBinding hasSmoothed behavior).
            env = x; prevIn = x; primed = true;
            out = remap(env);
            return out;
        }

        // character: ±2 octaves of envelope speed; the spiky side (>0) also
        // boosts rising transients and arms the hard-change bypass.
        float c = clampf(p.character, -1.0f, 1.0f);
        float rateMul = std::exp2(2.0f * c);           // c=0 → 1.0 (neutral)
        float spike = 0.0f;
        if (c > 0.0f) {
            float d = (x - prevIn) / dt;               // rising-edge derivative
            spike = c * clamp01(d * 0.05f);
        }
        prevIn = x;
        float target = std::min(x + spike, 1.5f);

        // hard-change bypass (explicit param, or armed by chopped character)
        float hardTh = p.hardChangeThreshold;
        if (c > 0.0f) hardTh = std::min(hardTh, 1.0f - 0.65f * c); // → 0.35 at c=1

        if (std::fabs(target - env) > hardTh) {
            env = target;
            holdT = p.holdSec;
        } else if (target > env) {
            env += (target - env) * shapedK(dt, p.attackSec / rateMul,
                                            p.attackPow, target - env);
            holdT = p.holdSec;
        } else {
            if (holdT > 0.0f) holdT -= dt;             // peak-hold
            else env += (target - env) * shapedK(dt, p.releaseSec / rateMul,
                                                 p.releasePow, env - target);
        }

        // remap + mandatory micro-slew
        out += (remap(env) - out) * k1(dt, p.microSlewSec);
        return out;
    }

    float value() const { return out; }

private:
    // Exponential approach coefficient with a state-based curve-shape warp:
    // pow=1 → exactly k1 (legacy); pow>1 slows near the target (ease-in),
    // pow<1 rushes it (ease-out). dt-independent because the warp depends on
    // the remaining distance, not the frame rate.
    static float shapedK(float dt, float tau, float powExp, float dist) {
        float k = k1(dt, tau);
        if (powExp != 1.0f) {
            float dnorm = clampf(std::fabs(dist), 1e-3f, 1.0f);
            k = clamp01(k * std::pow(dnorm, powExp - 1.0f));
        }
        return k;
    }

    float remap(float v) const {
        float y = (v - p.inLo) / std::max(p.inHi - p.inLo, 1e-6f);
        y = clamp01(y);
        if (p.gamma != 1.0f) y = std::pow(y, std::max(p.gamma, 0.01f));
        return p.outLo + y * (p.outHi - p.outLo);
    }
};

// ── dB-domain AGC (UE + Synesthesia) ─────────────────────────────────────
// norm = clamp((dB − noiseFloor) / range, 0, 1). The −60 dB noise floor
// doubles as the silence gate. `range` auto-tracks: grows fast (0.1 s) to
// accommodate peaks, shrinks ultra-slowly (60 s) — no pumping. Replaces the
// fixed magic gains (Easel 60/100/200/400× and the 4× mic seed).
struct DbAGC {
    float noiseFloorDb = -60.0f;
    float rangeDb      = 40.0f;   // live auto-tracked headroom above the floor
    float minRangeDb   = 30.0f;   // keeps quiet noise from pinning to 1.0
    float maxRangeDb   = 60.0f;
    float growSec      = 0.1f;
    float shrinkSec    = 60.0f;

    float norm(float lin, float dt) {
        float db = 20.0f * std::log10(std::max(lin, 1e-7f));
        float over = db - noiseFloorDb;
        if (over > rangeDb) rangeDb += (over - rangeDb) * k1(dt, growSec);
        else                rangeDb += (minRangeDb - rangeDb) * k1(dt, shrinkSec);
        rangeDb = clampf(rangeDb, minRangeDb, maxRangeDb);
        return clamp01(over / std::max(rangeDb, 1e-3f));
    }

    // 3-state indicator (Synesthesia): 0 = no signal, 1 = ok, 2 = clipping.
    int state(float lastNorm) const {
        if (lastNorm <= 0.001f) return 0;
        if (lastNorm >= 0.985f) return 2;
        return 1;
    }
};

// ── Dual-follower onset (Hit temperament) ────────────────────────────────
// Fast follower crossing a slow follower ⇒ onset (level-independent, no
// absolute threshold — feed it PRE-AGC linear energy). Output is an AD
// envelope: attack 10 ms pow 1.0, release 350 ms pow 1.6 (phase-tracked, so
// the shape powers are exact).
struct HitDetector {
    float fastTau = 0.015f, slowTau = 0.40f;
    float threshMul = 1.15f;       // fast > slow*threshMul (+ tiny abs eps)
    float absEps = 1e-4f;          // rejects pure numeric noise near silence
    float refractorySec = 0.10f;
    float attackSec = 0.010f, attackPow = 1.0f;
    float releaseSec = 0.350f, releasePow = 1.6f;

    // state
    float fast = 0.0f, slow = 0.0f;
    float phase = 1e9f;            // seconds since last trigger
    float refT = 0.0f;

    float update(float x, float dt) {
        if (!(dt > 0.0f)) dt = 1.0f / 60.0f;
        fast += (x - fast) * k1(dt, fastTau);
        slow += (x - slow) * k1(dt, slowTau);
        if (refT > 0.0f) refT -= dt;
        if (refT <= 0.0f && fast > slow * threshMul + absEps) {
            phase = 0.0f;
            refT = refractorySec;
        }
        phase += dt;
        if (phase < attackSec)
            return std::pow(phase / attackSec, std::max(attackPow, 0.01f));
        float r = 1.0f - (phase - attackSec) / std::max(releaseSec, 1e-3f);
        return r <= 0.0f ? 0.0f : std::pow(r, std::max(releasePow, 0.01f));
    }

    bool triggeredNow() const { return phase <= 1e-6f; }
};

// ── Presence (slow macro envelope) ───────────────────────────────────────
struct PresenceEnv {
    float attackSec = 1.5f, releaseSec = 4.0f;
    float v = 0.0f;
    float update(float x, float dt) {
        v += (x - v) * k1(dt, x > v ? attackSec : releaseSec);
        return v;
    }
};

// ── Tempo tracker ────────────────────────────────────────────────────────
// Autocorrelation over the onset-strength envelope, resampled to a fixed
// internal 100 Hz clock (decoupled from render rate). 50–220 BPM with
// octave correction and a confidence output; the caller phase-locks the
// product BPM clock with it (tap/OSC stay overrides).
struct TempoTracker {
    static constexpr int   kRate   = 100;     // Hz internal sampling
    static constexpr int   kBufLen = 1024;    // ~10 s history
    static constexpr int   kWin    = 600;     // 6 s analysis window
    static constexpr float kEvalPeriod = 0.5f;

    float buf[kBufLen] = {};
    int   w = 0;
    long long total = 0;
    float acc = 0.0f, tickMax = 0.0f;
    float sinceEval = 0.0f;

    float bpm = 0.0f;          // stable, octave-corrected output (0 = unknown)
    float confidence = 0.0f;   // 0..1
    float lastRawBpm = 0.0f;
    int   agree = 0;

    void feed(float onsetStrength, float dt) {
        if (!(dt > 0.0f)) return;
        if (dt > 0.25f) dt = 0.25f;
        tickMax = std::max(tickMax, onsetStrength);
        acc += dt;
        const float tick = 1.0f / kRate;
        while (acc >= tick) {
            acc -= tick;
            buf[w] = tickMax;
            w = (w + 1) % kBufLen;
            total++;
            tickMax = onsetStrength;
        }
        sinceEval += dt;
        if (sinceEval >= kEvalPeriod && total >= kWin) {
            sinceEval = 0.0f;
            evaluate();
        }
    }

private:
    void evaluate() {
        float x[kWin];
        float mean = 0.0f;
        for (int i = 0; i < kWin; i++) {
            x[i] = buf[(w - kWin + i + kBufLen) % kBufLen];
            mean += x[i];
        }
        mean /= (float)kWin;
        float energy = 0.0f;
        for (int i = 0; i < kWin; i++) { x[i] -= mean; energy += x[i] * x[i]; }
        if (energy < 1e-6f) {            // silence / no rhythm — decay trust
            confidence *= 0.5f;
            agree = 0;
            return;
        }

        const int minLag = 27;           // 222 BPM
        const int maxLag = 120;          // 50 BPM
        float r[maxLag + 2];
        float best = -1.0f;
        int bestLag = 0;
        for (int lag = minLag; lag <= maxLag; lag++) {
            float s = 0.0f;
            for (int i = lag; i < kWin; i++) s += x[i] * x[i - lag];
            s /= energy;                 // normalized −1..1
            r[lag] = s;
            // mild log-domain prior centered on 120 BPM to break octave ties
            float lagBpm = 60.0f * kRate / (float)lag;
            float pr = std::exp(-0.5f * std::pow(std::log2(lagBpm / 120.0f) / 1.2f, 2.0f));
            float score = s * (0.7f + 0.3f * pr);
            if (score > best) { best = score; bestLag = lag; }
        }
        if (bestLag <= 0 || r[bestLag] <= 0.02f) {
            confidence *= 0.5f;
            agree = 0;
            return;
        }

        // octave correction: fold extremes back toward 90–180 when the
        // harmonic lag explains the signal nearly as well.
        auto rAt = [&](int l) { return (l >= minLag && l <= maxLag) ? r[l] : -1.0f; };
        float curBpm = 60.0f * kRate / (float)bestLag;
        if (curBpm < 90.0f && rAt(bestLag / 2) > 0.72f * r[bestLag]) bestLag /= 2;
        else if (curBpm > 180.0f && rAt(bestLag * 2) > 0.72f * r[bestLag]) bestLag *= 2;

        // parabolic refinement around the (possibly folded) peak
        float lagF = (float)bestLag;
        if (bestLag > minLag && bestLag < maxLag) {
            float y0 = r[bestLag - 1], y1 = r[bestLag], y2 = r[bestLag + 1];
            float den = y0 - 2.0f * y1 + y2;
            if (std::fabs(den) > 1e-9f) lagF += 0.5f * (y0 - y2) / den;
        }
        float newBpm = clampf(60.0f * kRate / lagF, 50.0f, 220.0f);

        // stability vote: only trust a tempo confirmed across evaluations
        if (bpm > 0.0f && std::fabs(newBpm - bpm) < 3.0f) {
            agree = std::min(agree + 1, 8);
            bpm += (newBpm - bpm) * 0.25f;     // gentle refinement
        } else if (std::fabs(newBpm - lastRawBpm) < 3.0f) {
            if (++agree >= 2) bpm = newBpm;    // two consistent votes → adopt
        } else {
            agree = std::max(agree - 2, 0);
        }
        lastRawBpm = newBpm;

        float rawConf = clamp01(r[bestLag] * 1.6f);
        confidence = rawConf * clamp01(agree / 3.0f);
    }
};

} // namespace easelaudio
