#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "dsp/osc/PolyBlepOscillator.h"

TEST_CASE ("sine oscillator runs at the requested frequency")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Sine);
    osc.setFrequency (440.0f);
    osc.reset (0.0f);

    int risingCrossings = 0;
    float prev = osc.processSample();
    for (int i = 1; i < 48000; ++i)        // exactly 1 second
    {
        const float s = osc.processSample();
        if (prev < 0.0f && s >= 0.0f) ++risingCrossings;
        prev = s;
    }
    REQUIRE (risingCrossings >= 439);
    REQUIRE (risingCrossings <= 441);
}

TEST_CASE ("saw oscillator output stays bounded")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Saw);
    osc.setFrequency (220.0f);
    osc.reset (0.0f);

    for (int i = 0; i < 96000; ++i)
    {
        const float s = osc.processSample();
        REQUIRE (s >= -1.05f);
        REQUIRE (s <=  1.05f);
    }
}
