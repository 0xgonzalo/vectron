#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/mod/AdsrEnvelope.h"

namespace
{
    AdsrEnvelope make (float a, float d, float s, float r, double sr = 48000.0)
    {
        AdsrEnvelope env;
        env.setSampleRate (sr);
        env.setParameters ({ a, d, s, r });
        return env;
    }

    // advance n samples, return last value
    float run (AdsrEnvelope& env, int n)
    {
        float v = 0.0f;
        for (int i = 0; i < n; ++i) v = env.getNextSample();
        return v;
    }
}

TEST_CASE ("attack reaches full level within the attack time")
{
    auto env = make (0.1f, 0.2f, 0.5f, 0.1f);
    env.noteOn();
    REQUIRE (run (env, 4800) >= 0.99f);          // 0.1 s @ 48 kHz
}

TEST_CASE ("attack segment is exponential (convex: midpoint above linear)")
{
    auto env = make (0.1f, 0.2f, 0.5f, 0.1f);
    env.noteOn();
    const float mid = run (env, 2400);           // half the attack time
    REQUIRE (mid > 0.55f);                       // linear would be ~0.5
}

TEST_CASE ("decay is exponential (concave: midpoint below linear) and lands on sustain")
{
    auto env = make (0.0f, 0.2f, 0.4f, 0.1f);
    env.noteOn();
    (void) env.getNextSample();                  // instant attack -> 1.0
    const float mid = run (env, 4800);           // half the decay time
    REQUIRE (mid < 0.68f);                       // linear midpoint would be ~0.7
    REQUIRE (std::abs (run (env, 9600) - 0.4f) < 0.01f);
}

TEST_CASE ("sustain holds until noteOff, then release decays to inactive")
{
    auto env = make (0.0f, 0.01f, 0.6f, 0.05f);
    env.noteOn();
    run (env, 4800);
    REQUIRE (std::abs (env.getCurrentValue() - 0.6f) < 0.01f);
    env.noteOff();
    run (env, 48000 / 10);                        // 0.1 s >> 0.05 s release
    REQUIRE (env.getCurrentValue() < 1.0e-3f);
    REQUIRE_FALSE (env.isActive());
}

TEST_CASE ("retrigger is click-free: attack restarts from the current value")
{
    auto env = make (0.5f, 0.2f, 0.8f, 0.5f);
    env.noteOn();
    run (env, 10000);
    const float before = env.getCurrentValue();
    env.noteOn();                                 // retrigger mid-attack
    const float after = env.getNextSample();
    REQUIRE (std::abs (after - before) < 0.01f);
}

TEST_CASE ("zero-length segments are instant and stable")
{
    auto env = make (0.0f, 0.0f, 0.5f, 0.0f);
    env.noteOn();
    (void) env.getNextSample();                   // attack -> 1
    REQUIRE (std::abs (run (env, 2) - 0.5f) < 0.01f);   // decay -> sustain
    env.noteOff();
    (void) env.getNextSample();
    REQUIRE (env.getCurrentValue() < 1.0e-3f);
    REQUIRE_FALSE (env.isActive());
}

TEST_CASE ("idle envelope outputs zero and reports inactive")
{
    auto env = make (0.1f, 0.1f, 0.5f, 0.1f);
    REQUIRE (env.getNextSample() == 0.0f);
    REQUIRE_FALSE (env.isActive());
}
