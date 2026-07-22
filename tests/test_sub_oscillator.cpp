#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/osc/SubOscillator.h"

static int risingZeroCrossings (SubOscillator& o, int n)
{
    int c = 0;
    float prev = o.processSample();
    for (int i = 1; i < n; ++i)
    {
        const float s = o.processSample();
        if (prev < 0.0f && s >= 0.0f) ++c;
        prev = s;
    }
    return c;
}

TEST_CASE ("sub octave -1 halves the frequency")
{
    SubOscillator o;
    o.setSampleRate (48000.0);
    o.setWave (SubOscillator::Wave::Sine);
    o.setNoteFrequency (440.0f);
    o.setOctave (-1);
    o.noteOn();
    const int c = risingZeroCrossings (o, 48000);   // expect ~220
    REQUIRE (c >= 218);
    REQUIRE (c <= 222);
}

TEST_CASE ("sub octave -2 quarters the frequency")
{
    SubOscillator o;
    o.setSampleRate (48000.0);
    o.setWave (SubOscillator::Wave::Sine);
    o.setNoteFrequency (440.0f);
    o.setOctave (-2);
    o.noteOn();
    const int c = risingZeroCrossings (o, 48000);   // expect ~110
    REQUIRE (c >= 108);
    REQUIRE (c <= 112);
}

TEST_CASE ("sub square has near-zero DC mean")
{
    SubOscillator o;
    o.setSampleRate (48000.0);
    o.setWave (SubOscillator::Wave::Square);
    o.setNoteFrequency (220.0f);
    o.setOctave (-1);                 // 110 Hz
    o.noteOn();
    double sum = 0.0;
    const int n = 48000;
    for (int i = 0; i < n; ++i) sum += o.processSample();
    REQUIRE (std::abs (sum / n) < 0.02);
}

TEST_CASE ("sub output stays bounded for all waves")
{
    for (auto w : { SubOscillator::Wave::Sine, SubOscillator::Wave::Triangle, SubOscillator::Wave::Square })
    {
        SubOscillator o;
        o.setSampleRate (48000.0);
        o.setWave (w);
        o.setNoteFrequency (440.0f);
        o.setOctave (-1);
        o.noteOn();
        for (int i = 0; i < 48000; ++i)
        {
            const float s = o.processSample();
            REQUIRE (s >= -1.1f);
            REQUIRE (s <=  1.1f);
        }
    }
}
