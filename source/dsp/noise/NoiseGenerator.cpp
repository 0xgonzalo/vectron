#include "dsp/noise/NoiseGenerator.h"
#include <cmath>
#include <algorithm>

// Per-color makeup gains bring each color to roughly unit RMS so the morph keeps
// a steady perceived level (see design spec). Tune during the Task 7 listening pass.
static constexpr float kWhiteScale = 1.0f;
static constexpr float kPinkScale  = 0.25f;
static constexpr float kBrownScale = 10.0f;

void NoiseGenerator::setSampleRate (double sr) noexcept
{
    sampleRate = sr;
    tunedBp.setSampleRate (sr);
    noiseFilter.setSampleRate (sr);
    const float tau = 0.015f * (float) sr;                  // 15 ms
    smoothCoef = (sr > 0.0) ? (1.0f - std::exp (-1.0f / tau)) : 1.0f;
    levelCurrent  = levelTarget;
    cutoffCurrent = cutoffTarget;
    noiseFilter.setCutoff (cutoffCurrent);
    setShRate (shRate);
    setShGlide (shGlide);
    updateTuned();
}

float NoiseGenerator::whiteSample() noexcept
{
    rng = rng * 1664525u + 1013904223u;
    const float u = (float) ((rng >> 8) & 0xFFFFFFu) / 16777216.0f;   // [0,1)
    return u * 2.0f - 1.0f;                                           // [-1,1)
}

float NoiseGenerator::processSample() noexcept
{
    const float white = whiteSample();

    // Pink — Paul Kellet one-pole network.
    pb0 = 0.99765f * pb0 + white * 0.0990460f;
    pb1 = 0.96300f * pb1 + white * 0.2965164f;
    pb2 = 0.57000f * pb2 + white * 1.0526913f;
    const float pink = pb0 + pb1 + pb2 + white * 0.1848f;

    // Brown — leaky integrator.
    brownState = (brownState + 0.02f * white) / 1.02f;
    const float brown = brownState;

    const float w = white * kWhiteScale;
    const float p = pink  * kPinkScale;
    const float b = brown * kBrownScale;

    float colorOut;
    if (color <= 0.5f)
    {
        const float t = color * 2.0f;
        colorOut = w * (1.0f - t) + p * t;
    }
    else
    {
        const float t = (color - 0.5f) * 2.0f;
        colorOut = p * (1.0f - t) + b * t;
    }

    // Sample & Hold (mod source) — samples the color output; unrouted until Phase 5.
    shPhase += shInc;
    if (shPhase >= 1.0f) { shPhase -= 1.0f; shTarget = colorOut; }
    shValue += (shTarget - shValue) * shGlideCoef;

    // Smooth the always-on filter cutoff per sample (zipper-free automation).
    cutoffCurrent += (cutoffTarget - cutoffCurrent) * smoothCoef;
    noiseFilter.setCutoff (cutoffCurrent);

    // Tuned band-pass (optional) then the always-on noise filter.
    float out = tuned ? tunedBp.processSample (colorOut) : colorOut;
    out = noiseFilter.processSample (out);

    // Smooth the output level per sample.
    levelCurrent += (levelTarget - levelCurrent) * smoothCoef;
    return out * levelCurrent;
}

void NoiseGenerator::setNoiseFilter (FilterType type, float cutoffHz, float reso) noexcept
{
    SvfFilter::Mode m = SvfFilter::Mode::LP;
    switch (type)
    {
        case FilterType::HP: m = SvfFilter::Mode::HP; break;
        case FilterType::BP: m = SvfFilter::Mode::BP; break;
        case FilterType::LP: m = SvfFilter::Mode::LP; break;
    }
    noiseFilter.setMode (m);
    noiseFilter.setResonance (reso);
    cutoffTarget = cutoffHz;
}

void NoiseGenerator::updateTuned() noexcept
{
    // Keytrack blends (in log-frequency) between a fixed A4 pivot and the note.
    const float ref     = 440.0f;
    const float logHz   = (1.0f - keytrack) * std::log (ref)
                        + keytrack * std::log (std::max (1.0f, noteHz));
    const float trackHz = std::exp (logHz);
    const float centre  = trackHz * std::pow (2.0f, tunedPitch / 12.0f);
    tunedBp.setMode (SvfFilter::Mode::BP);
    tunedBp.setResonance (0.95f);     // high Q -> narrow, "tuned"
    tunedBp.setCutoff (centre);
}

void NoiseGenerator::setShRate (float hz) noexcept
{
    shRate = hz;
    shInc = (sampleRate > 0.0) ? (float) (hz / sampleRate) : 0.0f;
}

void NoiseGenerator::setShGlide (float g) noexcept
{
    shGlide = g;
    if (g <= 0.0f || sampleRate <= 0.0)
    {
        shGlideCoef = 1.0f;
    }
    else
    {
        const float tau = g * 0.05f * (float) sampleRate;   // up to ~50 ms at glide = 1
        shGlideCoef = 1.0f - std::exp (-1.0f / tau);
    }
}

void NoiseGenerator::reset() noexcept
{
    pb0 = pb1 = pb2 = 0.0f;
    brownState = 0.0f;
    tunedBp.reset();
    noiseFilter.reset();
    shPhase = 0.0f; shTarget = 0.0f; shValue = 0.0f;
    levelCurrent  = levelTarget;
    cutoffCurrent = cutoffTarget;
}
