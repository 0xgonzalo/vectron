#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include "dsp/filter/SvfFilter.h"

// Steady-state RMS gain of the filter at a given sine frequency.
static float rmsGainAt (SvfFilter& f, float freqHz, double sr)
{
    f.reset();
    const int n    = (int) sr;       // 1 second
    const int skip = n / 4;          // discard transient
    double num = 0.0, den = 0.0;
    const float w = 2.0f * 3.14159265358979f * freqHz / (float) sr;
    for (int i = 0; i < n; ++i)
    {
        const float x = std::sin (w * i);
        const float y = f.processSample (x);
        if (i >= skip) { num += (double) y * y; den += (double) x * x; }
    }
    return (float) std::sqrt (num / den);
}

TEST_CASE ("SVF lowpass passes lows and cuts highs")
{
    SvfFilter f;
    f.setSampleRate (48000.0);
    f.setMode (SvfFilter::Mode::LP);
    f.setResonance (0.0f);
    f.setCutoff (1000.0f);

    REQUIRE (rmsGainAt (f, 100.0f,   48000.0) > 0.7f);
    REQUIRE (rmsGainAt (f, 12000.0f, 48000.0) < 0.2f);
}

TEST_CASE ("SVF highpass cuts lows and passes highs")
{
    SvfFilter f;
    f.setSampleRate (48000.0);
    f.setMode (SvfFilter::Mode::HP);
    f.setResonance (0.0f);
    f.setCutoff (1000.0f);

    REQUIRE (rmsGainAt (f, 100.0f,  48000.0) < 0.2f);
    REQUIRE (rmsGainAt (f, 8000.0f, 48000.0) > 0.7f);
}

TEST_CASE ("SVF bandpass peaks near cutoff")
{
    SvfFilter f;
    f.setSampleRate (48000.0);
    f.setMode (SvfFilter::Mode::BP);
    f.setResonance (0.5f);
    f.setCutoff (1000.0f);

    const float atCenter = rmsGainAt (f, 1000.0f,  48000.0);
    const float belowC   = rmsGainAt (f, 100.0f,   48000.0);
    const float aboveC   = rmsGainAt (f, 10000.0f, 48000.0);
    REQUIRE (atCenter > belowC);
    REQUIRE (atCenter > aboveC);
}

TEST_CASE ("SVF stays bounded under fast cutoff modulation")
{
    SvfFilter f;
    f.setSampleRate (48000.0);
    f.setMode (SvfFilter::Mode::LP);
    f.setResonance (0.9f);
    f.reset();

    uint32_t rng = 1u;
    for (int i = 0; i < 96000; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        const float x = (float) ((rng >> 8) & 0xFFFFFFu) / 16777216.0f * 2.0f - 1.0f; // [-1,1)
        const float cutoff = 100.0f + 15000.0f * (0.5f + 0.5f * std::sin (0.01f * i));
        f.setCutoff (cutoff);
        const float y = f.processSample (x);
        REQUIRE (std::isfinite (y));
        REQUIRE (std::abs (y) < 100.0f);
    }
}
