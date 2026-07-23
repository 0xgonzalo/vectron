#pragma once
#include <cmath>
#include <cstdint>

// Full-featured mod LFO (PRD §6.2). Phase is an unwrapped double in cycles;
// S&H / Random-smooth derive their random values from a hash of the cycle
// index, so output is a pure function of phase — deterministic across voices
// in Global mode and safe under absolute-phase jumps. JUCE-free.
class ModLfo
{
public:
    enum class Shape    { Sine, Triangle, SawUp, SawDown, Square, SampleHold, RandomSmooth };
    enum class Polarity { Bipolar, Unipolar };

    void setSampleRate (double sr) noexcept        { sampleRate = sr > 0.0 ? sr : 44100.0; recalc(); }
    void setRate (float hz) noexcept               { rateHz = hz; recalc(); }
    void setShape (Shape s) noexcept               { shape = s; }
    void setPolarity (Polarity p) noexcept         { polarity = p; }
    void setPhaseOffsetDegrees (float deg) noexcept{ phaseOffset = deg / 360.0f; }
    void setFadeInSeconds (float s) noexcept       { fadeSeconds = s; recalc(); }
    void setSeed (uint32_t s) noexcept             { seed = s; }

    double getPhaseIncrement() const noexcept      { return increment; }

    // Poly-mode note-on: restart phase at the offset and restart the fade.
    void retrigger() noexcept      { phase = (double) phaseOffset; startFadeIn(); }
    // Global-mode note-on: fade restarts, phase untouched.
    void startFadeIn() noexcept    { fade = fadeInc >= 1.0f ? 1.0f : 0.0f; }
    // Global-mode block sync: absolute master phase in cycles (unwrapped).
    void setAbsolutePhase (double cycles) noexcept { phase = cycles + (double) phaseOffset; }

    float processSample() noexcept
    {
        const float v = valueAt (phase) * fade;
        phase += increment;
        fade += fadeInc;
        if (fade > 1.0f) fade = 1.0f;
        return polarity == Polarity::Unipolar ? 0.5f * (v + 1.0f) : v;
    }

private:
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr float kPi    = 3.1415926535897932f;

    static uint32_t hash (uint64_t x) noexcept              // splitmix64 finalizer
    {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return (uint32_t) (x ^ (x >> 31));
    }

    float randForCycle (int64_t k) const noexcept           // [-1, 1]
    {
        const uint32_t h = hash ((uint64_t) k ^ ((uint64_t) seed << 32));
        return (float) (h & 0xFFFFFFu) / 16777215.0f * 2.0f - 1.0f;
    }

    float valueAt (double ph) const noexcept
    {
        const auto  cycle = (int64_t) std::floor (ph);
        const float t     = (float) (ph - (double) cycle);   // fractional phase [0,1)
        switch (shape)
        {
            case Shape::Sine:       return std::sin (kTwoPi * t);
            case Shape::Triangle:   return t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
            case Shape::SawUp:      return 2.0f * t - 1.0f;
            case Shape::SawDown:    return 1.0f - 2.0f * t;
            case Shape::Square:     return t < 0.5f ? 1.0f : -1.0f;
            case Shape::SampleHold: return randForCycle (cycle);
            case Shape::RandomSmooth:
            {
                const float a = randForCycle (cycle);
                const float b = randForCycle (cycle + 1);
                const float e = 0.5f - 0.5f * std::cos (kPi * t);   // cosine ease
                return a + (b - a) * e;
            }
        }
        return 0.0f;
    }

    void recalc() noexcept
    {
        increment = (double) rateHz / sampleRate;
        fadeInc   = fadeSeconds > 1.0e-4f ? (float) (1.0 / (fadeSeconds * sampleRate)) : 2.0f;
    }

    double   sampleRate  = 44100.0;
    double   phase       = 0.0;      // cycles, unwrapped
    double   increment   = 0.0;
    float    rateHz      = 1.0f;
    float    phaseOffset = 0.0f;     // cycles
    float    fadeSeconds = 0.0f;
    float    fade        = 1.0f;
    float    fadeInc     = 2.0f;     // >= 1 -> no fade
    uint32_t seed        = 1u;
    Shape    shape       = Shape::Sine;
    Polarity polarity    = Polarity::Bipolar;
};
