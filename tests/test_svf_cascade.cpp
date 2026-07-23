#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/filter/SvfCascade.h"
#include "dsp/filter/SvfFilter.h"

// Peak output magnitude for a steady sine after warm-up.
static float sineMagnitude (SvfCascade& f, float freqHz, double sr = 48000.0)
{
    constexpr int warm = 4800, measure = 9600;
    const float twoPi = 6.283185307179586f;
    double phase = 0.0;
    float peak = 0.0f;
    for (int i = 0; i < warm + measure; ++i)
    {
        const float y = f.processSample (std::sin ((float) (twoPi * phase)));
        phase += freqHz / sr;
        if (phase >= 1.0) phase -= 1.0;
        if (i >= warm) peak = std::max (peak, std::abs (y));
    }
    return peak;
}

TEST_CASE ("24 dB LP rolls off much steeper than 12 dB two octaves above cutoff")
{
    SvfCascade f12, f24;
    for (auto* f : { &f12, &f24 })
    {
        f->setSampleRate (48000.0);
        f->setMode (SvfCascade::Mode::LP);
        f->setCutoff (1000.0f);
        f->setResonance (0.0f);
        f->setDrive (0.0f);
    }
    f12.setSlope24 (false);
    f24.setSlope24 (true);

    const float m12 = sineMagnitude (f12, 4000.0f);
    const float m24 = sineMagnitude (f24, 4000.0f);
    REQUIRE (m24 < m12 * 0.3f);           // theory: ~-24 dB vs ~-48 dB
    REQUIRE (m12 < 0.3f);                 // 12 dB slope is already well down
}

TEST_CASE ("cascaded notch kills the cutoff frequency, passes far below it")
{
    SvfCascade f;
    f.setSampleRate (48000.0);
    f.setMode (SvfCascade::Mode::Notch);
    f.setSlope24 (true);
    f.setCutoff (1000.0f);
    f.setResonance (0.0f);
    f.setDrive (0.0f);

    REQUIRE (sineMagnitude (f, 1000.0f) < 0.05f);
    f.reset();
    REQUIRE (sineMagnitude (f, 100.0f) > 0.7f);
}

TEST_CASE ("stable under per-sample cutoff sweeps at high resonance")
{
    SvfCascade f;
    f.setSampleRate (48000.0);
    f.setMode (SvfCascade::Mode::LP);
    f.setSlope24 (true);
    f.setResonance (0.9f);
    f.setDrive (0.0f);

    float phase = 0.0f;
    for (int i = 0; i < 96000; ++i)
    {
        // log sweep 200 Hz -> 18 kHz -> 200 Hz, updated EVERY sample
        const float sweep = (i < 48000 ? i : 96000 - i) / 48000.0f;
        f.setCutoff (200.0f * std::pow (90.0f, sweep));
        phase += 110.0f / 48000.0f;
        if (phase >= 1.0f) phase -= 1.0f;
        const float y = f.processSample (2.0f * phase - 1.0f);   // naive saw input
        REQUIRE (std::abs (y) < 10.0f);
    }
}

TEST_CASE ("12 dB path with drive 0 is bit-identical to a plain SvfFilter")
{
    SvfCascade cascade;
    SvfFilter  plain;
    cascade.setSampleRate (48000.0);  plain.setSampleRate (48000.0);
    cascade.setMode (SvfCascade::Mode::BP); plain.setMode (SvfFilter::Mode::BP);
    cascade.setSlope24 (false);
    cascade.setCutoff (2500.0f);      plain.setCutoff (2500.0f);
    cascade.setResonance (0.5f);      plain.setResonance (0.5f);
    cascade.setDrive (0.0f);

    unsigned int lcg = 1u;
    for (int i = 0; i < 4096; ++i)
    {
        lcg = lcg * 1664525u + 1013904223u;
        const float x = (float) (lcg >> 8) / 8388608.0f - 1.0f;
        REQUIRE (cascade.processSample (x) == plain.processSample (x));
    }
}
