#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/osc/VectorEngine.h"

using Xfade = VectorEngine::Xfade;

static float weightSum (VectorEngine::Weights w) { return w.a + w.b + w.c + w.d; }

TEST_CASE ("bilinear weights sum to one in both modes")
{
    for (float x = -1.0f; x <= 1.0f; x += 0.25f)
        for (float y = -1.0f; y <= 1.0f; y += 0.25f)
        {
            REQUIRE (std::abs (weightSum (VectorEngine::computeWeights (x, y, Xfade::Linear))     - 1.0f) < 1.0e-5f);
            REQUIRE (std::abs (weightSum (VectorEngine::computeWeights (x, y, Xfade::EqualPower)) - 1.0f) < 1.0e-5f);
        }
}

TEST_CASE ("each plane corner isolates its oscillator")
{
    const auto a = VectorEngine::computeWeights (-1.0f,  1.0f, Xfade::Linear);  // top-left  = A
    const auto b = VectorEngine::computeWeights ( 1.0f,  1.0f, Xfade::Linear);  // top-right = B
    const auto c = VectorEngine::computeWeights (-1.0f, -1.0f, Xfade::Linear);  // bot-left  = C
    const auto d = VectorEngine::computeWeights ( 1.0f, -1.0f, Xfade::Linear);  // bot-right = D

    REQUIRE (a.a > 0.999f); REQUIRE (a.b < 0.001f); REQUIRE (a.c < 0.001f); REQUIRE (a.d < 0.001f);
    REQUIRE (b.b > 0.999f); REQUIRE (b.a < 0.001f); REQUIRE (b.c < 0.001f); REQUIRE (b.d < 0.001f);
    REQUIRE (c.c > 0.999f); REQUIRE (c.a < 0.001f); REQUIRE (c.b < 0.001f); REQUIRE (c.d < 0.001f);
    REQUIRE (d.d > 0.999f); REQUIRE (d.a < 0.001f); REQUIRE (d.b < 0.001f); REQUIRE (d.c < 0.001f);
}

TEST_CASE ("equal-power keeps the center balanced")
{
    const auto w = VectorEngine::computeWeights (0.0f, 0.0f, Xfade::EqualPower);
    REQUIRE (std::abs (w.a - 0.25f) < 1.0e-5f);
    REQUIRE (std::abs (w.b - 0.25f) < 1.0e-5f);
    REQUIRE (std::abs (w.c - 0.25f) < 1.0e-5f);
    REQUIRE (std::abs (w.d - 0.25f) < 1.0e-5f);
}
