#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/mod/ModLfo.h"

namespace
{
    ModLfo make (ModLfo::Shape s, float rate = 1.0f, double sr = 1000.0)
    {
        ModLfo l;
        l.setSampleRate (sr);
        l.setRate (rate);
        l.setShape (s);
        l.retrigger();
        return l;
    }
}

TEST_CASE ("saw up traverses -1..1 over one cycle")
{
    auto l = make (ModLfo::Shape::SawUp);          // 1 Hz @ 1 kHz -> 1000 samples/cycle
    REQUIRE (std::abs (l.processSample() - (-1.0f)) < 1.0e-4f);   // phase 0
    for (int i = 0; i < 499; ++i) (void) l.processSample();
    REQUIRE (std::abs (l.processSample() - 0.0f) < 5.0e-3f);      // phase 0.5
}

TEST_CASE ("sine hits known values and square flips at half phase")
{
    auto sine = make (ModLfo::Shape::Sine);
    REQUIRE (std::abs (sine.processSample()) < 1.0e-4f);          // sin(0)
    for (int i = 0; i < 249; ++i) (void) sine.processSample();
    REQUIRE (sine.processSample() > 0.99f);                        // sin(pi/2)

    auto sq = make (ModLfo::Shape::Square);
    REQUIRE (sq.processSample() == 1.0f);
    for (int i = 0; i < 499; ++i) (void) sq.processSample();
    REQUIRE (sq.processSample() == -1.0f);
}

TEST_CASE ("phase offset shifts the start point")
{
    ModLfo l;
    l.setSampleRate (1000.0);
    l.setRate (1.0f);
    l.setShape (ModLfo::Shape::SawUp);
    l.setPhaseOffsetDegrees (180.0f);
    l.retrigger();
    REQUIRE (std::abs (l.processSample() - 0.0f) < 5.0e-3f);      // saw at phase 0.5
}

TEST_CASE ("unipolar output stays in [0,1]")
{
    auto l = make (ModLfo::Shape::Sine, 7.0f);
    l.setPolarity (ModLfo::Polarity::Unipolar);
    for (int i = 0; i < 5000; ++i)
    {
        const float v = l.processSample();
        REQUIRE (v >= 0.0f);
        REQUIRE (v <= 1.0f);
    }
}

TEST_CASE ("fade-in ramps amplitude over the configured time")
{
    ModLfo l;
    l.setSampleRate (1000.0);
    l.setRate (0.0f);                             // freeze phase at 0
    l.setShape (ModLfo::Shape::Square);           // value +1 at phase 0
    l.setFadeInSeconds (1.0f);
    l.retrigger();
    (void) l.processSample();                     // fade starts near 0
    for (int i = 0; i < 498; ++i) (void) l.processSample();
    const float half = l.processSample();
    REQUIRE (half > 0.4f);
    REQUIRE (half < 0.6f);                        // ~halfway through the fade
    for (int i = 0; i < 600; ++i) (void) l.processSample();
    REQUIRE (l.processSample() == 1.0f);          // fade complete
}

TEST_CASE ("sample & hold is constant within a cycle, deterministic per cycle index")
{
    auto a = make (ModLfo::Shape::SampleHold, 1.0f);
    const float first = a.processSample();
    for (int i = 0; i < 900; ++i)
        REQUIRE (a.processSample() == first);     // same cycle -> held

    auto b = make (ModLfo::Shape::SampleHold, 1.0f);
    REQUIRE (b.processSample() == first);         // same seed + cycle -> same value
}

TEST_CASE ("random smooth is continuous (bounded per-sample delta)")
{
    auto l = make (ModLfo::Shape::RandomSmooth, 5.0f);
    float prev = l.processSample();
    for (int i = 0; i < 3000; ++i)
    {
        const float v = l.processSample();
        REQUIRE (std::abs (v - prev) < 0.1f);
        prev = v;
    }
}

TEST_CASE ("setAbsolutePhase reproduces an identical stream in a second instance")
{
    auto a = make (ModLfo::Shape::RandomSmooth, 3.0f);
    for (int i = 0; i < 777; ++i) (void) a.processSample();

    auto b = make (ModLfo::Shape::RandomSmooth, 3.0f);
    b.setAbsolutePhase (777.0 * a.getPhaseIncrement());
    // Tolerance: 'a' accumulated phase by repeated addition, 'b' jumped by a
    // product — identical to within float rounding, not bitwise.
    for (int i = 0; i < 500; ++i)
        REQUIRE (std::abs (a.processSample() - b.processSample()) < 1.0e-4f);
}
