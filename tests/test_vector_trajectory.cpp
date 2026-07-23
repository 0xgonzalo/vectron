#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/osc/VectorTrajectory.h"

using Catch::Approx;
using vectron::TrajectoryModel;
using vectron::TrajectoryMacros;
using vectron::TrajectoryPlayhead;

namespace
{
    // Default spec path: A(-1,+1) -> B(+1,+1) -> D(+1,-1) -> C(-1,-1), 500 ms / 1 beat each.
    TrajectoryModel cornersModel()
    {
        TrajectoryModel m;
        m.numPoints = 4;
        const float xs[4] { -1.0f, 1.0f, 1.0f, -1.0f };
        const float ys[4] {  1.0f, 1.0f, -1.0f, -1.0f };
        for (int i = 0; i < 4; ++i)
        {
            m.points[i].x = xs[i];
            m.points[i].y = ys[i];
            m.points[i].timeMs = 500.0f;
            m.points[i].beats  = 1.0f;
        }
        return m;
    }

    struct Pos { float x, y; };
    Pos adv (TrajectoryPlayhead& ph, const TrajectoryModel& m, const TrajectoryMacros& mac, float dt)
    {
        Pos p { 0, 0 };
        ph.advance (m, mac, dt, p.x, p.y);
        return p;
    }
}

TEST_CASE ("playhead starts at P0 on note-on", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 1;              // One-Shot
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 0.0f);
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (1.0f));
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Travel);
}

TEST_CASE ("one-shot travels the path and holds at Pn", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 1;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);

    auto p = adv (ph, m, mac, 0.25f);                // mid P0->P1
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (1.0f));

    p = adv (ph, m, mac, 0.5f);                      // t=0.75 s: mid P1->P2
    REQUIRE (p.x == Approx (1.0f));
    REQUIRE (p.y == Approx (0.0f).margin (1e-5));

    p = adv (ph, m, mac, 10.0f);                     // way past the end
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (-1.0f));
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Holding);

    p = adv (ph, m, mac, 1.0f);                      // stays held
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (-1.0f));
}

TEST_CASE ("rate is a speed multiplier", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 1; mac.rate = 2.0f;   // 250 ms per segment
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 0.125f);               // mid P0->P1
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (1.0f));
}

TEST_CASE ("sync uses beats x secondsPerBeat, still scaled by rate", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 1; mac.sync = true; mac.secondsPerBeat = 0.25f;  // 240 bpm
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    auto p = adv (ph, m, mac, 0.125f);                     // mid P0->P1 (0.25 s per segment)
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));

    mac.rate = 2.0f;                                       // now 0.125 s per segment
    TrajectoryPlayhead ph2;
    ph2.noteOn (m, mac);
    p = adv (ph2, m, mac, 0.0625f);
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
}

TEST_CASE ("fewer than 2 points holds at P0", "[trajectory]")
{
    TrajectoryModel m;
    m.numPoints = 1;
    m.points[0].x = 0.5f; m.points[0].y = -0.5f;
    TrajectoryMacros mac; mac.mode = 1;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 5.0f);
    REQUIRE (p.x == Approx (0.5f));
    REQUIRE (p.y == Approx (-0.5f));
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Holding);
}

TEST_CASE ("segment duration clamps to a 1 ms floor", "[trajectory]")
{
    auto m = cornersModel();
    m.points[1].timeMs = 1.0f;
    TrajectoryMacros mac; mac.mode = 1; mac.rate = 4.0f;   // 0.25 ms -> clamped to 1 ms
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 0.0005f);              // half of the clamped duration
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (1.0f));
}

TEST_CASE ("dt = 0 queries the position without advancing", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 1;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    adv (ph, m, mac, 0.25f);
    const auto a = adv (ph, m, mac, 0.0f);
    const auto b = adv (ph, m, mac, 0.0f);
    REQUIRE (a.x == b.x);
    REQUIRE (a.y == b.y);
}

TEST_CASE ("mode Off never advances", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 0;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 1.0f);
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (1.0f));
}
