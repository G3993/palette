#pragma once
#include <glad/glad.h>

// ─────────────────────────────────────────────────────────────────────────
// AudioFeatures — the shared "Audio Feature Bus".
//
// One struct, computed once per frame, carrying every perceptual audio
// feature into every shader through a single setter + a single uniform
// block. Adding a new feature touches exactly: this struct, AudioAnalyzer
// (compute), Application (assemble), ShaderSource::translateFragment
// (declare), ShaderSource::uploadUniforms (upload). The setter signature
// never changes again.
//
// All scalars are normalized 0..1 unless noted (tilt/energyVel/etc. are
// -1..1, palette anchors are linear RGB). Defaults are NEUTRAL so a shader
// reading an as-yet-uncomputed feature gets a sane value, never garbage.
// ─────────────────────────────────────────────────────────────────────────
struct AudioFeatures {
    // ── Tier 1 — core energy & bands ──────────────────────────────────
    float level = 0;       // overall loudness ("the breath")
    float sub = 0;         // sub-bass
    float bass = 0;        // low-end body
    float lowMid = 0;      // warmth / pad+vox body
    float highMid = 0;     // vocal presence / leads
    float treble = 0;      // air / hats / sparkle
    float punch = 0;       // transient snap (crest, peak-held)
    float beat = 0;        // onset envelope (decaying flash)
    float beatPhase = 0;   // sawtooth per beat (anticipation)
    float beatPulse = 0;   // decaying pulse per beat boundary
    float barPhase = 0;    // sawtooth over 4 beats
    float bpm = 0;         // tempo (raw)
    float tempo01 = 0;     // normalized tempo (60..180 -> 0..1)

    // ── Tier 2 — spectral character ───────────────────────────────────
    float brightness = 0;  // spectral centroid: dark(0) -> bright(1)
    float spread = 0;      // focused(0) -> washy(1)
    float rolloff = 0;     // body(0) -> air(1)
    float flatness = 0;    // tonal(0) -> noise(1)
    float flux = 0;        // spectral change / movement
    float onset = 0;       // discrete attack pulse
    float onsetRate = 0;   // rhythmic busyness
    float tilt = 0;        // warm(-1) <-> harsh(+1)
    float zcr = 0;         // sibilance / cheap noisiness
    float texture = 0.5f;  // smooth(0) <-> crispy(1)

    // ── Tier 3 — affect / mood (neutral defaults) ─────────────────────
    float valence = 0.5f;  // sad/dark(0) -> bright/pleasant(1)
    float arousal = 0;     // calm(0) -> energetic(1)
    float tension = 0;     // resolved(0) -> unstable(1)
    float warmth = 0.5f;   // cold/airy(0) -> warm/intimate(1)
    float softness = 0.5f; // sharp(0) -> gentle/blurred(1)
    float roughness = 0;   // sensory dissonance
    float charm = 0.05f;   // "lovely groove" sweet spot

    // ── Tier 4 — structure / build-up ─────────────────────────────────
    float energy = 0;      // arrangement altitude (slow)
    float energyVel = 0;   // rising(+)/falling(-) (-1..1)
    float energyAcc = 0;   // lift / anticipation (-1..1)
    float buildup = 0;     // riser progress (integrating)
    float buildupRate = 0; // signed build slope (-1..1)
    float drop = 0;        // structural impact impulse
    float novelty = 0;     // section-change signal
    float sectionPhase = 0;// section index mod N / N
    float sectionAge = 0;  // seconds-into-section (~/60s)
    float layers = 0;      // active-element count (assembly)
    float density = 0;     // mix fullness
    float presence[4] = {0, 0, 0, 0}; // per-band presence bass/lowMid/highMid/treble
    float flow[2] = {0, 0};           // continuous drift vector

    // ── Tier 5 — palette anchors (linear RGB; neutral grey ramp default)
    float palShadow[3] = {0.05f, 0.05f, 0.06f};
    float palMid[3]    = {0.30f, 0.30f, 0.33f};
    float palHigh[3]   = {0.80f, 0.80f, 0.84f};
    float palAccent[3] = {0.90f, 0.85f, 0.70f};
    float palTemp = 0;     // global warm/cool grade (-1..1)
    float palSat = 0.3f;   // global vividness

    // ── Harmony + textures ────────────────────────────────────────────
    float chroma[12] = {0,0,0,0,0,0,0,0,0,0,0,0}; // pitch-class energies
    float dominantPitch = 0; // strongest pitch class / 12 (STEP signal)
    float majorMinor = 0.5f; // minor(0) <-> major(1)
    float hcdf = 0;          // harmonic change (chord-change) signal
    GLuint fftTex = 0;       // 128x1 R8 spectrum
    GLuint chromaTex = 0;    // 12x1 chroma (0 until palette engine lands)
    GLuint occupancyTex = 0; // 12x1 per-band occupancy

    // ── EaselAudio v1 (additive — spec/easel_audio_bus.json) ──────────
    // Temperament matrix: Hit (dual-follower onset, AD 10ms/350ms),
    // Presence (slow macro envelope 1.5s/4s), Time (monotonic clock
    // += dt*band — UNCLAMPED, pauses in silence, never jitters).
    float bassHit = 0, midHit = 0, highHit = 0;
    float bassPresence = 0, midPresence = 0, highPresence = 0;
    float levelPresence = 0;  // mix presence (uniform audioLevelPresence —
                              // schema's "audioPresence" name is taken by the
                              // existing vec4 per-band presence uniform)
    float bassTime = 0, midTime = 0, highTime = 0;
    float levelTime = 0;      // uniform audioTime (mix clock)

    // Rhythm bus (detected tempo phase-locks BPMSync; tap/OSC override)
    float bpmConfidence = 0;  // <0.4 → beat-locked signals fall back to level
    float phase2 = 0, phase4 = 0, phase8 = 0, phase16 = 0; // multi-beat ramps
    float onBeat = 0;         // logistic-eased one-shot (~120ms)
    float toggleOnBeat = 0;   // eased 0/1 flip each beat

    // Tier-1 pseudo-stems (LR-style band split + causal median HPSS)
    float stemBass = 0, stemDrums = 0, stemMelody = 0, stemAir = 0, stemVocal = 0;
    float stemBassHit = 0, stemDrumsHit = 0, stemMelodyHit = 0, stemAirHit = 0, stemVocalHit = 0;
    float stemBassPresence = 0, stemDrumsPresence = 0, stemMelodyPresence = 0,
          stemAirPresence = 0, stemVocalPresence = 0;
};
