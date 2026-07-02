#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/osc/VectorLfo.h"

TEST_CASE ("sine LFO runs at the requested rate")
{
    VectorLfo lfo;
    lfo.setSampleRate (48000.0);
    lfo.setShape (VectorLfo::Shape::Sine);
    lfo.setRate (5.0f);
    lfo.setDepth (1.0f);
    lfo.reset();

    int risingCrossings = 0;
    float prev = lfo.processSample();
    for (int i = 1; i < 48000; ++i)
    {
        const float s = lfo.processSample();
        if (prev < 0.0f && s >= 0.0f) ++risingCrossings;
        prev = s;
    }
    REQUIRE (risingCrossings >= 4);
    REQUIRE (risingCrossings <= 6);
}

TEST_CASE ("depth scales LFO amplitude and bounds output")
{
    VectorLfo lfo;
    lfo.setSampleRate (48000.0);
    lfo.setShape (VectorLfo::Shape::Sine);
    lfo.setRate (2.0f);
    lfo.setDepth (0.5f);
    lfo.reset();

    float maxAbs = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const float s = lfo.processSample();
        maxAbs = std::max (maxAbs, std::abs (s));
        REQUIRE (s >= -0.5001f);
        REQUIRE (s <=  0.5001f);
    }
    REQUIRE (maxAbs > 0.49f);
}

TEST_CASE ("square LFO only emits plus/minus depth")
{
    VectorLfo lfo;
    lfo.setSampleRate (48000.0);
    lfo.setShape (VectorLfo::Shape::Square);
    lfo.setRate (3.0f);
    lfo.setDepth (0.8f);
    lfo.reset();

    for (int i = 0; i < 48000; ++i)
    {
        const float s = lfo.processSample();
        REQUIRE ((std::abs (s - 0.8f) < 1.0e-5f || std::abs (s + 0.8f) < 1.0e-5f));
    }
}

TEST_CASE ("sample-and-hold holds value between rate ticks")
{
    VectorLfo lfo;
    lfo.setSampleRate (48000.0);
    lfo.setShape (VectorLfo::Shape::SampleHold);
    lfo.setRate (10.0f);          // one step = 4800 samples
    lfo.setDepth (1.0f);
    lfo.reset();

    float first = lfo.processSample();
    int changes = 0;
    float prev = first;
    for (int i = 1; i < 4800; ++i)   // within the first step: must hold
    {
        const float s = lfo.processSample();
        if (std::abs (s - prev) > 1.0e-6f) ++changes;
        REQUIRE (s >= -1.0001f);
        REQUIRE (s <=  1.0001f);
        prev = s;
    }
    REQUIRE (changes == 0);          // constant within a step

    // Over 2 seconds (~20 steps) the value must change at least a few times.
    int stepChanges = 0;
    float held = prev;
    for (int i = 0; i < 96000; ++i)
    {
        const float s = lfo.processSample();
        if (std::abs (s - held) > 1.0e-6f) { ++stepChanges; held = s; }
    }
    REQUIRE (stepChanges >= 5);
}
