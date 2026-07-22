#pragma once
#include <cstdint>
#include "dsp/filter/SvfFilter.h"

// Featured multi-color noise generator: White->Pink->Brown morph, optional
// pitch-tracked band-pass ("tuned" noise), an always-on HP/BP/LP filter, and a
// Sample & Hold value exposed as a mod source (unrouted until Phase 5). JUCE-free.
class NoiseGenerator
{
public:
    enum class FilterType { HP, BP, LP };   // order matches the noise_filterType choice

    void setSampleRate (double sr) noexcept;
    void setColor (float c) noexcept { color = (c < 0.0f) ? 0.0f : (c > 1.0f ? 1.0f : c); }
    void setLevel (float l) noexcept { level = l; }

    void setTuned (bool on) noexcept            { tuned = on; }
    void setTunedPitch (float semis) noexcept   { tunedPitch = semis; updateTuned(); }
    void setKeytrack (float percent) noexcept   { keytrack = percent * 0.01f; updateTuned(); }
    void setNoteFrequency (float hz) noexcept   { noteHz = hz; updateTuned(); }

    void setNoiseFilter (FilterType type, float cutoffHz, float reso) noexcept;

    void setShRate (float hz) noexcept;
    void setShGlide (float g) noexcept;

    void reset() noexcept;

    float processSample() noexcept;                          // filtered, level-scaled noise
    float getSampleHold() const noexcept { return shValue; } // mod source (Phase 5)

private:
    void  updateTuned() noexcept;
    float whiteSample() noexcept;                            // deterministic LCG in [-1,1)

    double sampleRate = 44100.0;
    float  color      = 0.0f;
    float  level      = 0.0f;

    // color-path state
    float    pb0 = 0.0f, pb1 = 0.0f, pb2 = 0.0f;             // Kellet pink poles
    float    brownState = 0.0f;                              // leaky integrator
    uint32_t rng = 0x1234567u;

    // tuned band-pass
    bool      tuned      = false;
    float     tunedPitch = 0.0f;
    float     keytrack   = 1.0f;                             // 0..1
    float     noteHz     = 440.0f;
    SvfFilter tunedBp;

    // always-on noise filter
    SvfFilter noiseFilter;

    // sample & hold
    float shPhase = 0.0f, shInc = 0.0f;
    float shTarget = 0.0f, shValue = 0.0f, shGlideCoef = 1.0f;
    float shRate = 5.0f, shGlide = 0.0f;
};
