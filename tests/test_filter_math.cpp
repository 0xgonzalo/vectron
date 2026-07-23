#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "dsp/filter/FilterMath.h"

using Catch::Matchers::WithinRel;
using vectron::effectiveCutoffHz;

TEST_CASE ("keytrack follows the keyboard around MIDI 60")
{
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60,  100.0f, 0.0f, 0.0f), WithinRel (1000.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 72,  100.0f, 0.0f, 0.0f), WithinRel (2000.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 48,  100.0f, 0.0f, 0.0f), WithinRel ( 500.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 72, -100.0f, 0.0f, 0.0f), WithinRel ( 500.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 84,    0.0f, 0.0f, 0.0f), WithinRel (1000.0f, 1e-5f));
}

TEST_CASE ("envelope amount spans +/-5 octaves at full depth")
{
    REQUIRE_THAT (effectiveCutoffHz ( 500.0f, 60, 0.0f, 1.0f,  1.0f), WithinRel (16000.0f,  1e-4f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60, 0.0f, 1.0f, -1.0f), WithinRel (31.25f,    1e-4f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60, 0.0f, 0.5f,  1.0f), WithinRel (5656.854f, 1e-4f));
}

TEST_CASE ("result clamps to the audio range")
{
    REQUIRE (effectiveCutoffHz (1000.0f, 60, 0.0f, 1.0f,  1.0f) == 20000.0f);   // 32k -> clamp
    REQUIRE (effectiveCutoffHz ( 100.0f, 60, 0.0f, 1.0f, -1.0f) == 20.0f);      // 3.125 -> clamp
}

TEST_CASE ("matrix modOct adds octaves on top of keytrack + env, still clamped")
{
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60, 0.0f, 0.0f, 0.0f,  1.0f), WithinRel (2000.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60, 0.0f, 0.0f, 0.0f, -2.0f), WithinRel ( 250.0f, 1e-5f));
    REQUIRE (effectiveCutoffHz (1000.0f, 60, 0.0f, 0.0f, 0.0f,  10.0f) == 20000.0f);
    REQUIRE (effectiveCutoffHz (1000.0f, 60, 0.0f, 0.0f, 0.0f, -10.0f) == 20.0f);
}
