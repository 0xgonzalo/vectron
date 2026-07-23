#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include "dsp/drive/DriveShaper.h"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE ("drive amount 0 is an exact identity for every type")
{
    for (auto type : { DriveShaper::Type::Tanh, DriveShaper::Type::Hard, DriveShaper::Type::Foldback })
    {
        DriveShaper d;
        d.setType (type);
        d.setAmount (0.0f);
        d.setTrimDb (0.0f);
        for (float x : { -2.0f, -0.5f, 0.0f, 0.3f, 1.7f })
            REQUIRE (d.processSample (x) == x);
    }
}

TEST_CASE ("tanh and hard clip are monotonic and bounded at full drive")
{
    for (auto type : { DriveShaper::Type::Tanh, DriveShaper::Type::Hard })
    {
        DriveShaper d;
        d.setType (type);
        d.setAmount (1.0f);
        d.setTrimDb (0.0f);
        float prev = d.processSample (-3.0f);
        for (float x = -2.99f; x <= 3.0f; x += 0.01f)
        {
            const float y = d.processSample (x);
            REQUIRE (y >= prev - 1.0e-6f);          // non-decreasing
            REQUIRE (std::abs (y) <= 1.0f + 1.0e-6f);
            prev = y;
        }
    }
}

TEST_CASE ("foldback folds back past the boundary and stays bounded")
{
    DriveShaper d;
    d.setType (DriveShaper::Type::Foldback);
    d.setAmount (1.0f);                              // pre-gain 20x
    d.setTrimDb (0.0f);
    // g*x = 1.0 -> exactly 1.0 ; g*x = 1.5 -> folds back down to 0.5
    REQUIRE_THAT (d.processSample (0.05f),  WithinAbs (1.0f, 1.0e-4f));
    REQUIRE_THAT (d.processSample (0.075f), WithinAbs (0.5f, 1.0e-4f));
    for (float x = -5.0f; x <= 5.0f; x += 0.001f)
        REQUIRE (std::abs (d.processSample (x)) <= 1.0f + 1.0e-5f);
}

TEST_CASE ("trim applies dB gain at the output")
{
    DriveShaper d;
    d.setType (DriveShaper::Type::Hard);
    d.setAmount (0.0f);
    d.setTrimDb (-6.0f);
    REQUIRE_THAT (d.processSample (1.0f), WithinRel (0.501187f, 1.0e-4f));
    d.setTrimDb (6.0f);
    REQUIRE_THAT (d.processSample (0.25f), WithinRel (0.25f * 1.995262f, 1.0e-4f));
    d.setTrimGain (0.5f);
    REQUIRE (d.processSample (1.0f) == 0.5f);
}

TEST_CASE ("drive output is continuous as amount rises from zero")
{
    for (auto type : { DriveShaper::Type::Tanh, DriveShaper::Type::Hard, DriveShaper::Type::Foldback })
    {
        DriveShaper d;
        d.setType (type);
        d.setTrimDb (0.0f);
        const float x = 1.5f;                 // hot pre-VCA mix sample

        // Sharp check pinning the original bug directly: no step when leaving
        // amount 0. At amount 0.001, w = 0.025, so the delta is bounded by
        // w * |shaped - dry|, which is small for every type. Pre-fix, this
        // stepped straight to the fully-shaped value (~0.6 delta for Tanh at
        // x = 1.5), so 0.05 is comfortably tight enough to catch a regression
        // while leaving headroom for the legitimate small crossfade delta.
        d.setAmount (0.0f);
        const float y0 = d.processSample (x);
        d.setAmount (0.001f);
        REQUIRE (std::abs (d.processSample (x) - y0) < 0.05f);

        // Foldback is continuous but steep: |dy/da| <= ~91 at x=1.5 (fold slope
        // +/-1, chain rule), so ~0.09 per 0.001 step is legitimate; a real
        // discontinuity steps O(0.5).
        const float maxStep = (type == DriveShaper::Type::Foldback) ? 0.12f : 0.06f;

        d.setAmount (0.0f);
        float prev = d.processSample (x);
        for (float a = 0.001f; a <= 0.05f; a += 0.001f)
        {
            d.setAmount (a);
            const float y = d.processSample (x);
            REQUIRE (std::abs (y - prev) < maxStep);
            prev = y;
        }
    }
}
