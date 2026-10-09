#pragma once
#include <cmath>
#include <cstdlib>

namespace palette::live {

// Motion sources for Live: free-running or beat-locked oscillators. Output 0..1.
struct Lfo {
    enum class Shape { Sine, Triangle, Saw, Square, Drift };
    Shape shape = Shape::Sine;
    float rateHz = 0.2f;     // used when beatDiv == 0
    int beatDiv = 0;         // 0 = free; 1,2,4,8 = beats per cycle (tempo-locked)
    float phase = 0.0f;
    float driftA = 0, driftB = 0, driftT = 1; // smooth random walk state

    float step(float dt, float bpm) {
        float hz = beatDiv > 0 ? (bpm / 60.0f) / (float)beatDiv : rateHz;
        phase = std::fmod(phase + hz * dt, 1.0f);
        switch (shape) {
            case Shape::Sine: return 0.5f + 0.5f * std::sin(phase * 6.2831853f);
            case Shape::Triangle: return 1.0f - std::fabs(phase * 2.0f - 1.0f);
            case Shape::Saw: return phase;
            case Shape::Square: return phase < 0.5f ? 1.0f : 0.0f;
            case Shape::Drift: {
                // ease between random targets, one per cycle
                driftT += hz * dt;
                if (driftT >= 1.0f) { driftT = 0; driftA = driftB; driftB = (float)std::rand() / RAND_MAX; }
                float t = driftT; t = t * t * (3 - 2 * t);
                return driftA + (driftB - driftA) * t;
            }
        }
        return 0.5f;
    }
    static const char* shapeName(Shape s) {
        switch (s) { case Shape::Sine: return "Sine"; case Shape::Triangle: return "Triangle"; case Shape::Saw: return "Saw"; case Shape::Square: return "Square"; case Shape::Drift: return "Drift"; }
        return "";
    }
};

} // namespace palette::live
