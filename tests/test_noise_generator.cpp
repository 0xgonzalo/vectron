#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <algorithm>
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

TEST_CASE ("sample-and-hold holds between rate ticks with no glide")
{
    NoiseGenerator g;
    g.setSampleRate (48000.0);
    g.setColor (0.0f);
    g.setShRate (10.0f);        // one step = 4800 samples
    g.setShGlide (0.0f);

    g.processSample();
    float prev = g.getSampleHold();
    int changes = 0;
    for (int i = 1; i < 4700; ++i)          // safely within the first step
    {
        g.processSample();
        const float v = g.getSampleHold();
        if (std::abs (v - prev) > 1.0e-6f) ++changes;
        prev = v;
    }
    REQUIRE (changes == 0);

    int stepChanges = 0;
    float held = prev;
    for (int i = 0; i < 96000; ++i)         // ~2 s -> ~20 steps
    {
        g.processSample();
        const float v = g.getSampleHold();
        if (std::abs (v - held) > 1.0e-6f) { ++stepChanges; held = v; }
    }
    REQUIRE (stepChanges >= 5);
}

TEST_CASE ("sample-and-hold glide smooths transitions")
{
    NoiseGenerator g;
    g.setSampleRate (48000.0);
    g.setColor (0.0f);
    g.setShRate (10.0f);
    g.setShGlide (0.8f);

    g.processSample();
    float prev = g.getSampleHold();
    float maxDelta = 0.0f, minV = 1.0e9f, maxV = -1.0e9f;
    for (int i = 0; i < 96000; ++i)
    {
        g.processSample();
        const float v = g.getSampleHold();
        maxDelta = std::max (maxDelta, std::abs (v - prev));
        minV = std::min (minV, v);
        maxV = std::max (maxV, v);
        prev = v;
    }
    REQUIRE (maxDelta < 0.1f);        // smoothed: no instant jumps
    REQUIRE (maxV - minV > 0.05f);    // but still moving over time
}

TEST_CASE ("noise level change is smoothed, not instant")
{
    NoiseGenerator g;
    g.setSampleRate (48000.0);
    g.setColor (0.0f);
    g.setNoiseFilter (NoiseGenerator::FilterType::LP, 20000.0f, 0.0f);
    g.setLevel (1.0f);
    for (int i = 0; i < 48000; ++i) g.processSample();   // prime to full level

    g.setLevel (0.0f);                                    // drop level
    float maxAbsShort = 0.0f;
    for (int i = 0; i < 48; ++i)                          // first ~1 ms
        maxAbsShort = std::max (maxAbsShort, std::abs (g.processSample()));

    for (int i = 0; i < 9600; ++i) g.processSample();     // let it settle (~200 ms)
    float maxAbsLate = 0.0f;
    for (int i = 0; i < 480; ++i)
        maxAbsLate = std::max (maxAbsLate, std::abs (g.processSample()));

    REQUIRE (maxAbsShort > 0.05f);   // did NOT snap to silence instantly (smoothing active)
    REQUIRE (maxAbsLate  < 0.01f);   // fully faded after settling
}
