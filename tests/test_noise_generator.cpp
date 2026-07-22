#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/noise/NoiseGenerator.h"

// Ratio of first-difference energy to signal energy — a crude spectral-tilt metric.
// Larger = brighter (more HF, e.g. white); smaller = darker (e.g. brown). Scale-independent.
static float tiltRatio (NoiseGenerator& n, int numSamples)
{
    double sig = 0.0, diff = 0.0;
    float prev = n.processSample();
    for (int i = 1; i < numSamples; ++i)
    {
        const float s = n.processSample();
        sig += (double) s * s;
        const float d = s - prev;
        diff += (double) d * d;
        prev = s;
    }
    return (float) (diff / (sig + 1.0e-12));
}

static float rmsOf (NoiseGenerator& n, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = n.processSample();
        sum += (double) s * s;
    }
    return (float) std::sqrt (sum / numSamples);
}

// White in, transparent (wide-open) noise filter, tuned off, unit level.
static void configureColor (NoiseGenerator& g, float color)
{
    g.setSampleRate (48000.0);
    g.setColor (color);
    g.setTuned (false);
    g.setNoiseFilter (NoiseGenerator::FilterType::LP, 20000.0f, 0.0f);
    g.setLevel (1.0f);
}

TEST_CASE ("noise color morphs from bright (white) to dark (brown)")
{
    NoiseGenerator white, pink, brown;
    configureColor (white, 0.0f);
    configureColor (pink,  0.5f);
    configureColor (brown, 1.0f);

    const float rw = tiltRatio (white, 96000);
    const float rp = tiltRatio (pink,  96000);
    const float rb = tiltRatio (brown, 96000);

    REQUIRE (rw > rp * 1.2f);
    REQUIRE (rp > rb * 1.2f);
}

TEST_CASE ("noise output stays bounded across the color sweep")
{
    for (float c = 0.0f; c <= 1.0f; c += 0.1f)
    {
        NoiseGenerator g;
        configureColor (g, c);
        for (int i = 0; i < 48000; ++i)
        {
            const float s = g.processSample();
            REQUIRE (std::isfinite (s));
            REQUIRE (std::abs (s) < 4.0f);
        }
    }
}

TEST_CASE ("noise loudness stays in a sane range across the color sweep")
{
    NoiseGenerator w, p, b;
    configureColor (w, 0.0f);
    configureColor (p, 0.5f);
    configureColor (b, 1.0f);

    for (float r : { rmsOf (w, 48000), rmsOf (p, 48000), rmsOf (b, 48000) })
    {
        REQUIRE (r > 0.1f);
        REQUIRE (r < 3.0f);
    }
}

TEST_CASE ("tuned noise centroid tracks pitch")
{
    auto tiltForPitch = [] (float pitch)
    {
        NoiseGenerator g;
        g.setSampleRate (48000.0);
        g.setColor (0.0f);                                                    // white in
        g.setNoiseFilter (NoiseGenerator::FilterType::LP, 20000.0f, 0.0f);    // transparent
        g.setLevel (1.0f);
        g.setNoteFrequency (440.0f);
        g.setKeytrack (100.0f);
        g.setTunedPitch (pitch);
        g.setTuned (true);
        return tiltRatio (g, 96000);
    };
    // Higher tuned pitch -> BP centre higher -> brighter -> larger tilt ratio.
    REQUIRE (tiltForPitch (12.0f) > tiltForPitch (-12.0f));
}

TEST_CASE ("noise filter type shapes the spectrum")
{
    NoiseGenerator lp, hp;
    lp.setSampleRate (48000.0); lp.setColor (0.0f); lp.setTuned (false); lp.setLevel (1.0f);
    lp.setNoiseFilter (NoiseGenerator::FilterType::LP, 1000.0f, 0.0f);
    hp.setSampleRate (48000.0); hp.setColor (0.0f); hp.setTuned (false); hp.setLevel (1.0f);
    hp.setNoiseFilter (NoiseGenerator::FilterType::HP, 1000.0f, 0.0f);

    // LP on white -> darker (low tilt); HP on white -> brighter (high tilt).
    REQUIRE (tiltRatio (hp, 96000) > tiltRatio (lp, 96000));
}
