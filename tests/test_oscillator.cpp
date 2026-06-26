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

TEST_CASE ("pulse oscillator stays bounded")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Pulse);
    osc.setPulseWidth (0.5f);
    osc.setFrequency (220.0f);
    osc.reset (0.0f);

    for (int i = 0; i < 96000; ++i)
    {
        const float s = osc.processSample();
        REQUIRE (s >= -1.1f);
        REQUIRE (s <=  1.1f);
    }
}

TEST_CASE ("pulse duty cycle sets the DC mean")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Pulse);
    osc.setFrequency (100.0f);
    osc.reset (0.0f);

    auto meanFor = [&osc] (float pw)
    {
        osc.setPulseWidth (pw);
        osc.reset (0.0f);
        double sum = 0.0;
        const int n = 48000;
        for (int i = 0; i < n; ++i) sum += osc.processSample();
        return (float) (sum / n);
    };

    REQUIRE (std::abs (meanFor (0.5f) - 0.0f)  < 0.02f);   // 2*0.5-1 = 0
    REQUIRE (std::abs (meanFor (0.25f) + 0.5f) < 0.03f);   // 2*0.25-1 = -0.5
}
