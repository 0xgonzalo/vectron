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

TEST_CASE ("loop entry at loopStart == 0 starts looping immediately", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 2; mac.loopStart = 0; mac.loopEnd = 3;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Looping);
}

TEST_CASE ("forward loop wraps from loopEnd to loopStart", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 2; mac.loopStart = 1; mac.loopEnd = 3;  // loopDir Forward
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);

    auto p = adv (ph, m, mac, 0.5f);                 // attack P0->P1 done: at loop entry
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Looping);
    REQUIRE (p.x == Approx (1.0f));
    REQUIRE (p.y == Approx (1.0f));

    p = adv (ph, m, mac, 1.75f);                     // 1 full loop cycle (1.0 s) + 0.75 s
    // 0.75 s into the region: P1->P2 (0.5) then mid P2->P3
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (-1.0f));

    p = adv (ph, m, mac, 0.25f);                     // reach P3 == loopEnd -> wrap to P1
    REQUIRE (p.x == Approx (1.0f));
    REQUIRE (p.y == Approx (1.0f));
}

TEST_CASE ("reverse loop snaps to loopEnd and travels backward", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 2; mac.loopStart = 0; mac.loopEnd = 3; mac.loopDir = 2;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);

    auto p = adv (ph, m, mac, 0.0f);                 // entry snapped to P3
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (-1.0f));

    p = adv (ph, m, mac, 0.25f);                     // mid P3->P2
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (-1.0f));

    p = adv (ph, m, mac, 1.0f);                      // 1.25 s backward: mid P1->P0
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (1.0f));

    p = adv (ph, m, mac, 0.25f);                     // reach P0 == loopStart -> snap to P3
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (-1.0f));
}

TEST_CASE ("ping-pong bounces at both loop ends", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 2; mac.loopStart = 0; mac.loopEnd = 3; mac.loopDir = 1;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);

    auto p = adv (ph, m, mac, 1.75f);                // forward to P3 (1.5 s) + 0.25 s back
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));    // mid P3->P2
    REQUIRE (p.y == Approx (-1.0f));

    p = adv (ph, m, mac, 1.25f);                     // full backward leg ends at P0
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (1.0f));

    p = adv (ph, m, mac, 0.25f);                     // bounced forward: mid P0->P1
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (1.0f));
}

TEST_CASE ("loopStart >= loopEnd holds at loopStart", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 2; mac.loopStart = 2; mac.loopEnd = 2;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 5.0f);           // attack P0->P1->P2, then hold
    REQUIRE (p.x == Approx (1.0f));
    REQUIRE (p.y == Approx (-1.0f));
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Holding);
}

TEST_CASE ("loop indices clamp to the model size", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 2; mac.loopStart = 10; mac.loopEnd = 15;   // both clamp to 3
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 5.0f);
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (-1.0f));
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Holding);
}

TEST_CASE ("Loop mode ignores release", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 2; mac.loopStart = 0; mac.loopEnd = 3;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    ph.release();
    adv (ph, m, mac, 2.0f);
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Looping);
}

TEST_CASE ("loop+sustain exits forward to Pn on release", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 3; mac.loopStart = 1; mac.loopEnd = 2;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);

    auto p = adv (ph, m, mac, 0.75f);                // attack 0.5 + 0.25 into P1->P2
    REQUIRE (p.x == Approx (1.0f));
    REQUIRE (p.y == Approx (0.0f).margin (1e-5));
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Looping);

    ph.release();
    p = adv (ph, m, mac, 0.25f);                     // finishes P1->P2, now in the tail
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::ReleaseTail);

    p = adv (ph, m, mac, 0.25f);                     // mid P2->P3
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (-1.0f));

    p = adv (ph, m, mac, 0.25f);                     // holds at Pn
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (-1.0f));
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Holding);
}

TEST_CASE ("release while traveling backward flips forward with position continuity", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 3; mac.loopStart = 0; mac.loopEnd = 3; mac.loopDir = 2;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);                              // Reverse: snapped to P3, heading to P2

    auto p = adv (ph, m, mac, 0.35f);                // P3->P2 at phase 0.7
    REQUIRE (p.x == Approx (0.4f).margin (1e-4));
    REQUIRE (p.y == Approx (-1.0f));

    ph.release();
    p = adv (ph, m, mac, 0.0f);                      // flip is position-neutral
    REQUIRE (p.x == Approx (0.4f).margin (1e-4));
    REQUIRE (p.y == Approx (-1.0f));
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::ReleaseTail);

    p = adv (ph, m, mac, 0.35f);                     // finishes P2->P3 forward, holds at Pn
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (-1.0f));
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Holding);
}

TEST_CASE ("release during the attack skips the loop and travels to Pn", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 3; mac.loopStart = 2; mac.loopEnd = 3;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    adv (ph, m, mac, 0.25f);                         // mid P0->P1, before the loop
    ph.release();
    auto p = adv (ph, m, mac, 1.0f);                 // passes P1, P2 without looping
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));    // mid P2->P3
    REQUIRE (p.y == Approx (-1.0f));
    p = adv (ph, m, mac, 0.5f);
    REQUIRE (ph.getStage() == TrajectoryPlayhead::Stage::Holding);
    REQUIRE (p.x == Approx (-1.0f));
}

TEST_CASE ("latchFrom copies the master state (same-mode voices track it)", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros mac; mac.mode = 2; mac.loopStart = 0; mac.loopEnd = 3;
    TrajectoryPlayhead master;
    master.noteOn (m, mac);
    adv (master, m, mac, 0.6f);                      // P1->P2 at phase 0.2

    TrajectoryPlayhead voice;
    voice.latchFrom (master, mac);
    const auto pm = adv (master, m, mac, 0.2f);
    const auto pv = adv (voice,  m, mac, 0.2f);
    REQUIRE (pv.x == Approx (pm.x));
    REQUIRE (pv.y == Approx (pm.y));
}

TEST_CASE ("latchFrom with One-Shot mode travels forward to Pn from the latched phase", "[trajectory]")
{
    const auto m = cornersModel();
    TrajectoryMacros loopMac; loopMac.mode = 2; loopMac.loopStart = 0; loopMac.loopEnd = 3; loopMac.loopDir = 2;
    TrajectoryPlayhead master;
    master.noteOn (m, loopMac);                      // Reverse: P3 heading to P2
    adv (master, m, loopMac, 0.35f);                 // backward, phase 0.7

    TrajectoryMacros oneShot; oneShot.mode = 1;
    TrajectoryPlayhead voice;
    voice.latchFrom (master, oneShot);
    auto p = adv (voice, m, oneShot, 0.0f);          // same position after the flip
    REQUIRE (p.x == Approx (0.4f).margin (1e-4));
    p = adv (voice, m, oneShot, 0.35f);              // forward to P3 == Pn, holds
    REQUIRE (p.x == Approx (-1.0f));
    REQUIRE (p.y == Approx (-1.0f));
    REQUIRE (voice.getStage() == TrajectoryPlayhead::Stage::Holding);
}

namespace
{
    TrajectoryModel straightLineModel()              // P0(-1,0) -> P1(+1,0), 500 ms
    {
        TrajectoryModel m;
        m.numPoints = 2;
        m.points[0].x = -1.0f; m.points[0].y = 0.0f;
        m.points[1].x =  1.0f; m.points[1].y = 0.0f;
        m.points[1].timeMs = 500.0f;
        return m;
    }
}

TEST_CASE ("smooth interp eases timing with a cosine", "[trajectory]")
{
    const auto m = straightLineModel();
    TrajectoryMacros mac; mac.mode = 1; mac.interp = 1;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 0.125f);         // segPhase 0.25 -> eased t = 0.14645
    REQUIRE (p.x == Approx (-1.0f + 2.0f * 0.146447f).margin (1e-4));
    REQUIRE (p.y == Approx (0.0f).margin (1e-6));
}

TEST_CASE ("tension bows the segment perpendicular to the chord", "[trajectory]")
{
    auto m = straightLineModel();
    m.points[1].tension = 1.0f;
    TrajectoryMacros mac; mac.mode = 1; mac.interp = 1;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 0.25f);          // segPhase 0.5 -> eased t = 0.5
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (0.5f).margin (1e-4));    // ctrl (0, +1) -> apex y = 0.5

    m.points[1].tension = -1.0f;
    TrajectoryPlayhead ph2;
    ph2.noteOn (m, mac);
    const auto q = adv (ph2, m, mac, 0.25f);
    REQUIRE (q.y == Approx (-0.5f).margin (1e-4));   // opposite bow
}

TEST_CASE ("linear interp ignores tension", "[trajectory]")
{
    auto m = straightLineModel();
    m.points[1].tension = 1.0f;
    TrajectoryMacros mac; mac.mode = 1; mac.interp = 0;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 0.25f);
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (0.0f));
}

TEST_CASE ("bowed output clamps to the XY range", "[trajectory]")
{
    TrajectoryModel m;
    m.numPoints = 2;
    m.points[0].x = 0.0f; m.points[0].y = 1.0f;      // chord along the top edge
    m.points[1].x = 1.0f; m.points[1].y = 1.0f;
    m.points[1].timeMs = 500.0f;
    m.points[1].tension = 1.0f;                      // bows above y = +1
    TrajectoryMacros mac; mac.mode = 1; mac.interp = 1;
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 0.25f);          // apex would be y = 1.25
    REQUIRE (p.y == Approx (1.0f));                  // clamped
}

TEST_CASE ("tension bow is invariant to traversal direction", "[trajectory]")
{
    auto m = straightLineModel();
    m.points[1].tension = 1.0f;
    TrajectoryMacros mac; mac.mode = 2; mac.interp = 1;
    mac.loopStart = 0; mac.loopEnd = 1; mac.loopDir = 2;   // Reverse: travels P1 -> P0
    TrajectoryPlayhead ph;
    ph.noteOn (m, mac);
    const auto p = adv (ph, m, mac, 0.25f);                // phase 0.5, eased t = 0.5
    REQUIRE (p.x == Approx (0.0f).margin (1e-5));
    REQUIRE (p.y == Approx (0.5f).margin (1e-4));          // same bow side as forward travel
}
