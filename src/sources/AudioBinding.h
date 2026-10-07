#pragma once
#include "app/EaselAudio.h"
#include <cmath>

// Shared audio/MIDI parameter-binding types. Originally defined inside
// ShaderSource.h; factored out so non-shader sources (FluidSource, …) can
// reuse the exact same binding model + follower math behind the "sparkle"
// bind affordance in the PropertyPanel.

// Signal sources for parameter binding
enum class AudioSignal {
    None = 0,
    Level,   // RMS
    Bass,
    Mid,
    High,
    Beat,    // beat decay (0-1 pulse)
    MidiCC,  // MIDI control change (uses midiCC/midiChannel fields)
    // ── "Listening" signals — time-aware musical dynamics, not instantaneous
    // level. Sourced from the AudioAnalyzer's structure layer so the visuals
    // follow the SONG's arc (highs, lows, builds, drops, pauses) instead of
    // just twitching to the current loudness. APPENDED after MidiCC so the
    // serialized int values of the original signals never shift.
    Energy,    // slow arrangement altitude — the high<->low journey
    Build,     // riser / build-up progress — anticipation before a peak
    Drop,      // structural impact impulse — fires when a drop lands
    Silence,   // rises when the track goes quiet (pauses / breakdowns)
    Momentum,  // energy rising(>0.5) vs falling(<0.5), remapped to 0..1
};

// Per-parameter audio/MIDI binding
struct AudioBinding {
    AudioSignal signal = AudioSignal::None;
    float rangeMin = 0.0f;  // output min (maps to param min by default)
    float rangeMax = 1.0f;  // output max (maps to param max by default)
    // 0 = instant (snappy), 1 = very slow (heavy glide). Default 0.85 keeps new
    // bindings calm by default (was 0.7, then 0.55 — both still felt too
    // fast/strobey the moment audio is enabled). Users wanting punch drag it
    // down; the common case is "make it smooth," so start there.
    float smoothing = 0.80f;   // Lu: new bindings always start smooth
    // EaselAudio character: -1 = extra smooth … +1 = spiky/chopped.
    // 0 = NEUTRAL = the exact legacy feel (existing projects load with 0, so
    // nothing changes until the user touches the new Character control).
    float character = -1.0f;   // Lu: new bindings start on the smooth end
    float smoothedValue = 0.0f; // internal follower state (0-1, pre-range-map)
    bool  hasSmoothed  = false; // false until first sample (avoids 0 ramp-in)
    // Gentle-enable ramp: modulation DEPTH eases in over rampTime seconds
    // after the binding is created, so flipping reactivity on (or shuffling)
    // never slams the look — it fades from "no reaction" to full swing.
    // rampTime <= 0 disables. AudioPresetEngine::rebuild carries rampAge
    // across knob re-scales so only a true enable restarts the ramp.
    float rampAge  = 0.0f;
    float rampTime = 4.0f;
    // Response curve (cubic bezier from (0,0) to (1,1), control points P1/P2).
    // x = conditioned audio level (how much the selected signal is hitting),
    // y = fraction of the parameter's range that level produces. Linear
    // (the default) is the identity, i.e. exactly the legacy behavior; the
    // curve only applies once the user edits it (curveOn). Audio signals only.
    bool  curveOn = false;
    // Per-parameter input trim and decay speed (audio signals only). Both are
    // 1.0 = the legacy behavior. gain scales the raw signal before conditioning;
    // fall scales the release rate (>1 = drops back to 0 faster, so the
    // parameter pops up and down quicker; <1 = lingers).
    float gain = 1.0f;
    float fall = 1.0f;
    float lastRaw = 0.0f;   // gained raw level of the last sample (for UI meters)
    float curveP1x = 0.33f, curveP1y = 0.33f;
    float curveP2x = 0.66f, curveP2y = 0.66f;
    void resetCurve() { curveOn = false; curveP1x = 0.33f; curveP1y = 0.33f; curveP2x = 0.66f; curveP2y = 0.66f; }
    // Map x in [0,1] through the bezier (solve t for B_x(t)=x by bisection).
    float shape(float x) const {
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;
        float x1 = curveP1x < 0.0f ? 0.0f : (curveP1x > 1.0f ? 1.0f : curveP1x);
        float x2 = curveP2x < 0.0f ? 0.0f : (curveP2x > 1.0f ? 1.0f : curveP2x);
        float lo = 0.0f, hi = 1.0f, t = x;
        for (int i = 0; i < 20; i++) {
            t = 0.5f * (lo + hi);
            float u = 1.0f - t;
            float bx = 3.0f * u * u * t * x1 + 3.0f * u * t * t * x2 + t * t * t;
            if (bx < x) lo = t; else hi = t;
        }
        float u = 1.0f - t;
        float by = 3.0f * u * u * t * curveP1y + 3.0f * u * t * t * curveP2y + t * t * t;
        return by < 0.0f ? 0.0f : (by > 1.0f ? 1.0f : by);
    }
    // MIDI fields (used when signal == MidiCC)
    int midiCC = -1;        // CC number 0-127, -1 = unassigned
    int midiChannel = -1;   // MIDI channel 0-15, -1 = any
    // Soft takeover: a knob that sits somewhere else than the parameter must
    // first reach the parameter's position before it takes it — no jump on
    // the first touch after a (re)map. ccLastRaw = previous CC sample.
    bool  ccPickedUp = false;
    float ccLastRaw  = -1.0f;
    // Endless-knob mode: the CC is read as MOTION. Each change nudges a
    // target position from wherever the parameter is, so a knob never jumps
    // and its physical position is irrelevant. ccTarget01 = that position.
    float ccTarget01 = -1.0f;

    // Shared EaselAudio conditioning block (gate→attack→hold→release→
    // hard-change→character→remap→micro-slew). The legacy smoothing slider
    // maps onto its attack/release taus with the SAME rate curve as the old
    // inline follower, so a binding with character 0 behaves identically.
    easelaudio::Conditioner cond;

    // Frame-rate-independent asymmetric follower (punchy attack, softer
    // release), then map the conditioned 0..1 value onto [rangeMin, rangeMax].
    // `raw` is the 0..1 signal sample; returns the mapped output value (the
    // caller clamps it to the destination parameter's own range).
    float follow(float raw, float dt) {
        if (!(dt > 0.0f)) dt = 1.0f / 60.0f;
        if (dt > 0.1f)    dt = 0.1f;

        // MIDI knob path: NO audio conditioning. The gate/hold/asymmetric
        // release block (tuned for loudness envelopes) made a CC land ~0.3s
        // behind the hand on attack and ~0.6s on release at the default
        // smoothing — it read as MIDI "lag". A knob is already a human-
        // smoothed signal, so only a light symmetric slew survives, purely
        // to hide 7-bit zipper: smoothing slider 0 = instant, 1 = 60ms tau
        // (default 0.85 ≈ 50ms, settles within ~150ms).
        if (signal == AudioSignal::MidiCC) {
            float s = smoothing;
            if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f;
            if (!hasSmoothed) { smoothedValue = raw; hasSmoothed = true; }
            float tau = s * 0.13f;
            float a = (tau > 1e-4f) ? (1.0f - std::exp(-dt / tau)) : 1.0f;
            smoothedValue += (raw - smoothedValue) * a;
            return rangeMin + smoothedValue * (rangeMax - rangeMin);
        }

        raw *= gain;
        if (raw > 1.0f) raw = 1.0f; else if (raw < 0.0f) raw = 0.0f;
        lastRaw = raw;

        // Legacy smoothing-slider → rate mapping (fast ends roughly halved
        // vs the original 28/14 so even a snappy binding glides rather than
        // strobes — the "way too fast" fix). Unchanged math, now expressed
        // as the conditioning block's attack/release taus.
        constexpr float kAttackFast  = 14.0f, kAttackSlow  = 1.5f;
        constexpr float kReleaseFast = 7.0f,  kReleaseSlow = 0.7f;
        float s = smoothing;
        if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f;
        cond.p.setRates(kAttackFast  + (kAttackSlow  - kAttackFast)  * s,
                        (kReleaseFast + (kReleaseSlow - kReleaseFast) * s) * fall);
        cond.p.character = character;

        if (!hasSmoothed) { cond.reset(); hasSmoothed = true; }
        smoothedValue = cond.process(raw, dt);

        float shaped = curveOn ? shape(smoothedValue) : smoothedValue;
        float out = rangeMin + shaped * (rangeMax - rangeMin);
        // Depth ramp: swing grows from 0 → full around the range midpoint
        // with a smoothstep ease, so reactivity ALWAYS arrives gradually.
        if (rampTime > 0.0f && rampAge < rampTime) {
            rampAge += dt;
            float x = rampAge / rampTime;
            if (x > 1.0f) x = 1.0f;
            float ease = x * x * (3.0f - 2.0f * x);
            float mid = 0.5f * (rangeMin + rangeMax);
            out = mid + (out - mid) * ease;
        }
        return out;
    }
};
