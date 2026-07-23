# Vectron Phase 6 — Vector Trajectory Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The vector position travels a path of points across the XY plane over time (PRD §5.2.1) — point model in a ValueTree, per-voice playhead, One-Shot/Loop/Loop+Sustain modes, loop direction, interpolation, tempo sync, Per-Note/Global trigger, retrigger latch, `traj_depth` blend, and a `Traj Depth` mod-matrix destination.

**Architecture:** A JUCE-free leaf header `source/dsp/osc/VectorTrajectory.h` holds a POD `TrajectoryModel` (≤16 points) and a `TrajectoryPlayhead` advanced once per render block, its output smoothed per sample into the PRD §5.2 unified blend formula. Points persist in a `"TRAJECTORY"` ValueTree child of the APVTS state; the processor parses them on the message thread and hands the audio thread a snapshot via SpinLock try-lock + version counter. The processor also advances a free-running master playhead (always Loop semantics) for Global trigger and retrigger-off latch.

**Tech Stack:** JUCE 8.0.13, C++20, CMake ≥ 3.22, Catch2 v3.7.1 (unit), VectronSmoke console app (acceptance).

**Spec:** `docs/superpowers/specs/2026-07-23-vectron-phase-6-trajectory-design.md` — decision numbers below refer to its "Design decisions (locked)" list.

## Global Constraints

- Leaf DSP headers under `source/dsp/` include **no** JUCE headers (Catch2-tested only).
- No allocations or blocking locks in `processBlock` / voice render (SpinLock **try**-lock only on the audio thread).
- Param IDs `snake_case`; choice-array order must match enums — the enums are the contract; `static_assert` guards enforce name-array sizes.
- All work on branch `feat/phase-6-trajectory`; commit after every task.
- Verify with `cmake --build build` and `ctest --test-dir build --output-on-failure`. Unit binary: `./build/tests/VectronTests`. Smoke binary: `./build/tests/VectronSmoke_artefacts/Debug/VectronSmoke`. pluginval is NOT installed — the smoke harness is the acceptance gate.
- The claude-mem Read hook can truncate `Read` of previously-observed files — use `cat` via Bash to read source files fully; `Edit` still works afterwards.

## Shared interfaces (defined in Task 1, consumed everywhere)

```cpp
namespace vectron
{
struct TrajectoryModel
{
    struct Point
    {
        float x       = 0.0f;    // -1 .. +1
        float y       = 0.0f;    // -1 .. +1
        float timeMs  = 500.0f;  // travel time from the previous point (ignored on P0)
        float beats   = 1.0f;    // travel time in beats when synced (ignored on P0)
        float tension = 0.0f;    // -1 .. +1 bow of the incoming segment (0 = straight)
    };
    static constexpr int kMaxPoints = 16;
    Point points[kMaxPoints];
    int   numPoints = 0;
};

struct TrajectoryMacros           // choice indices match the APVTS choice arrays
{
    int   mode           = 0;     // 0 Off, 1 One-Shot, 2 Loop, 3 Loop+Sustain
    float rate           = 1.0f;  // 0.25 .. 4 speed multiplier (higher = faster)
    bool  sync           = false;
    float secondsPerBeat = 0.5f;  // 60 / bpm, resolved by the processor
    int   loopStart      = 0;
    int   loopEnd        = 3;
    int   loopDir        = 0;     // 0 Forward, 1 Ping-Pong, 2 Reverse
    int   interp         = 0;     // 0 Linear, 1 Smooth
};

class TrajectoryPlayhead
{
public:
    enum class Stage { Idle, Travel, Looping, ReleaseTail, Holding };
    void  noteOn (const TrajectoryModel&, const TrajectoryMacros&) noexcept;
    void  release() noexcept;                                             // Loop+Sustain exit
    void  latchFrom (const TrajectoryPlayhead& master, const TrajectoryMacros&) noexcept;
    void  advance (const TrajectoryModel&, const TrajectoryMacros&,
                   float dtSeconds, float& outX, float& outY) noexcept;   // dt 0 = position query
    Stage getStage() const noexcept;
};
}
```

---

### Task 1: Branch + `VectorTrajectory.h` core — One-Shot travel, timing, sync (decisions 1, 4, 6, 12)

**Files:**
- Create: `source/dsp/osc/VectorTrajectory.h`
- Create: `tests/test_vector_trajectory.cpp`
- Modify: `tests/CMakeLists.txt` (add the test file to `add_executable(VectronTests ...)`)

**Interfaces:**
- Consumes: nothing (leaf header).
- Produces: everything in "Shared interfaces" above. Semantics pinned here: segment `i-1→i` duration = `points[i].timeMs/1000 ÷ rate` (or `points[i].beats × secondsPerBeat ÷ rate` when synced), rate clamped to [0.25, 4], final duration clamped ≥ 1 ms. `numPoints < 2` → hold at P0. `mode == 0` (Off) → position query only, never advances.

- [ ] **Step 1: Create the branch**

```bash
git checkout -b feat/phase-6-trajectory
```

- [ ] **Step 2: Write the failing tests**

Create `tests/test_vector_trajectory.cpp`:

```cpp
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
```

In `tests/CMakeLists.txt`, add `test_vector_trajectory.cpp` to the `add_executable(VectronTests ...)` list (after `test_mod_matrix.cpp`).

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build build --target VectronTests 2>&1 | tail -5`
Expected: compile error — `dsp/osc/VectorTrajectory.h` not found.

- [ ] **Step 4: Write the implementation**

Create `source/dsp/osc/VectorTrajectory.h`:

```cpp
#pragma once
#include <cmath>

// Vector trajectory (PRD §5.2.1): point model + playhead. The vector position
// travels a path of points over time. JUCE-free leaf header — Catch2-tested.
// Loop / Loop+Sustain arrivals and latch land in later tasks of this phase.
namespace vectron
{
struct TrajectoryModel
{
    struct Point
    {
        float x       = 0.0f;    // -1 .. +1
        float y       = 0.0f;    // -1 .. +1
        float timeMs  = 500.0f;  // travel time from the previous point (ignored on P0)
        float beats   = 1.0f;    // travel time in beats when synced (ignored on P0)
        float tension = 0.0f;    // -1 .. +1 bow of the incoming segment (0 = straight)
    };
    static constexpr int kMaxPoints = 16;
    Point points[kMaxPoints];
    int   numPoints = 0;
};

struct TrajectoryMacros           // choice indices match the APVTS choice arrays
{
    int   mode           = 0;     // 0 Off, 1 One-Shot, 2 Loop, 3 Loop+Sustain
    float rate           = 1.0f;  // 0.25 .. 4 speed multiplier (higher = faster)
    bool  sync           = false;
    float secondsPerBeat = 0.5f;  // 60 / bpm, resolved by the processor
    int   loopStart      = 0;
    int   loopEnd        = 3;
    int   loopDir        = 0;     // 0 Forward, 1 Ping-Pong, 2 Reverse
    int   interp         = 0;     // 0 Linear, 1 Smooth
};

class TrajectoryPlayhead
{
public:
    enum class Stage { Idle, Travel, Looping, ReleaseTail, Holding };

    void noteOn (const TrajectoryModel& m, const TrajectoryMacros& mac) noexcept
    {
        released = false;
        from = 0; to = 1; segPhase = 0.0f;
        stage = Stage::Travel;
        if (m.numPoints < 2) { to = 0; stage = Stage::Holding; return; }
        maybeEnterLoop (m, mac);
    }

    void release() noexcept { released = true; }

    void latchFrom (const TrajectoryPlayhead& master, const TrajectoryMacros& mac) noexcept;

    void advance (const TrajectoryModel& m, const TrajectoryMacros& mac,
                  float dtSeconds, float& outX, float& outY) noexcept
    {
        if (m.numPoints < 1) { outX = 0.0f; outY = 0.0f; return; }
        clampIndices (m);
        if (m.numPoints < 2 || mac.mode == 0)
        {
            outputPosition (m, mac, outX, outY);
            return;
        }
        if (released && mac.mode == 3 && stage == Stage::Looping)
            exitLoopForward();
        float remaining = dtSeconds > 0.0f ? dtSeconds : 0.0f;
        int guard = 1024;                        // bound worst-case arrivals per call
        while (remaining > 0.0f && isMoving())
        {
            const float segDur   = segmentSeconds (m, mac);
            const float timeLeft = (1.0f - segPhase) * segDur;
            if (remaining < timeLeft) { segPhase += remaining / segDur; break; }
            remaining -= timeLeft;
            arrive (m, mac);
            if (--guard == 0) break;
        }
        outputPosition (m, mac, outX, outY);
    }

    Stage getStage() const noexcept { return stage; }

private:
    bool isMoving() const noexcept
    {
        return stage == Stage::Travel || stage == Stage::Looping || stage == Stage::ReleaseTail;
    }

    static float clamp1 (float v) noexcept { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    static int   clampIdx (int v, int last) noexcept { return v < 0 ? 0 : (v > last ? last : v); }

    void clampIndices (const TrajectoryModel& m) noexcept
    {
        const int last = m.numPoints - 1;
        from = clampIdx (from, last);
        to   = clampIdx (to,   last);
    }

    float segmentSeconds (const TrajectoryModel& m, const TrajectoryMacros& mac) const noexcept
    {
        const auto& p   = m.points[from > to ? from : to];   // the later point owns the segment
        const float rate = mac.rate < 0.25f ? 0.25f : (mac.rate > 4.0f ? 4.0f : mac.rate);
        const float base = mac.sync ? p.beats * mac.secondsPerBeat : p.timeMs * 0.001f;
        const float sec  = base / rate;
        return sec > 0.001f ? sec : 0.001f;                  // 1 ms floor (spec decision 12)
    }

    void maybeEnterLoop (const TrajectoryModel&, const TrajectoryMacros&) noexcept {}   // Task 2
    void exitLoopForward() noexcept {}                                                  // Task 3

    void arrive (const TrajectoryModel& m, const TrajectoryMacros& mac) noexcept
    {
        const int last = m.numPoints - 1;
        const int at = to;
        from = at; segPhase = 0.0f;
        if (stage == Stage::Travel)
        {
            maybeEnterLoop (m, mac);
            if (stage != Stage::Travel) return;              // entered loop or degenerate hold
            if (at >= last) { stage = Stage::Holding; to = at; return; }
            to = at + 1;
            return;
        }
        stage = Stage::Holding;                              // Looping/ReleaseTail: Tasks 2-3
        to = at;
    }

    void outputPosition (const TrajectoryModel& m, const TrajectoryMacros& mac,
                         float& ox, float& oy) const noexcept
    {
        (void) mac;                                          // Smooth interp lands in Task 4
        const auto& a = m.points[from];
        const auto& b = m.points[to];
        const float t = segPhase;
        ox = clamp1 (a.x + (b.x - a.x) * t);
        oy = clamp1 (a.y + (b.y - a.y) * t);
    }

    int   from = 0, to = 0;
    float segPhase = 0.0f;
    Stage stage = Stage::Idle;
    bool  released = false;
};

inline void TrajectoryPlayhead::latchFrom (const TrajectoryPlayhead&, const TrajectoryMacros&) noexcept
{
    // Task 3
}
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target VectronTests 2>&1 | tail -3 && ./build/tests/VectronTests "[trajectory]"`
Expected: All 8 trajectory test cases PASS.

- [ ] **Step 6: Run the full unit suite (no regressions)**

Run: `./build/tests/VectronTests | tail -3`
Expected: All test cases pass.

- [ ] **Step 7: Commit**

```bash
git add source/dsp/osc/VectorTrajectory.h tests/test_vector_trajectory.cpp tests/CMakeLists.txt
git commit -m "feat: trajectory model + playhead core — one-shot travel, timing, sync (Phase 6)"
```

---

### Task 2: Loop mode — Forward / Reverse / Ping-Pong, loop entry, degenerate regions (decisions 7, 8, 12)

**Files:**
- Modify: `source/dsp/osc/VectorTrajectory.h` (fill in `maybeEnterLoop`, extend `arrive`)
- Modify: `tests/test_vector_trajectory.cpp` (append tests)

**Interfaces:**
- Consumes: Task 1's playhead.
- Produces: `Stage::Looping` behavior. Forward: at `loopEnd` snap to `loopStart`. Reverse: loop entry snaps to `loopEnd`, travels backward, snaps back at `loopStart`. Ping-Pong: direction flips at both ends. `loopStart ≥ loopEnd` → `Stage::Holding` at `loopStart`. Loop mode ignores `release()`.

- [ ] **Step 1: Append the failing tests**

Append to `tests/test_vector_trajectory.cpp`:

```cpp
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
```

- [ ] **Step 2: Run tests to verify the new ones fail**

Run: `cmake --build build --target VectronTests 2>&1 | tail -3 && ./build/tests/VectronTests "[trajectory]"`
Expected: FAIL — loop cases hold instead of looping (Task 1's `arrive` stub).

- [ ] **Step 3: Implement loop arrivals**

In `source/dsp/osc/VectorTrajectory.h`, replace the `maybeEnterLoop` stub with:

```cpp
    // Called when sitting exactly on a point (segPhase == 0, from == current point).
    void maybeEnterLoop (const TrajectoryModel& m, const TrajectoryMacros& mac) noexcept
    {
        if (mac.mode != 2 && mac.mode != 3) return;
        if (mac.mode == 3 && released) return;               // released: behave one-shot (Task 3)
        const int last = m.numPoints - 1;
        const int ls = clampIdx (mac.loopStart, last);
        const int le = clampIdx (mac.loopEnd,   last);
        if (from < ls) return;                               // attack still traveling to the loop
        if (ls >= le)
        {
            stage = Stage::Holding;
            from = to = ls; segPhase = 0.0f;
            return;
        }
        if (mac.loopDir == 2) { from = le; to = le - 1; }    // Reverse: snap to loopEnd
        else                  { from = ls; to = ls + 1; }    // Forward / Ping-Pong
        segPhase = 0.0f;
        stage = Stage::Looping;
    }
```

and replace the whole `arrive` function with:

```cpp
    void arrive (const TrajectoryModel& m, const TrajectoryMacros& mac) noexcept
    {
        const int last = m.numPoints - 1;
        const int ls = clampIdx (mac.loopStart, last);
        const int le = clampIdx (mac.loopEnd,   last);
        const int travelDir = (to >= from) ? 1 : -1;
        const int at = to;
        from = at; segPhase = 0.0f;

        if (stage == Stage::Travel)
        {
            maybeEnterLoop (m, mac);
            if (stage != Stage::Travel) return;              // entered loop or degenerate hold
            if (at >= last) { stage = Stage::Holding; to = at; return; }
            to = at + 1;
            return;
        }
        if (stage == Stage::ReleaseTail)                     // Task 3
        {
            if (at >= last) { stage = Stage::Holding; to = at; return; }
            to = at + 1;
            return;
        }
        // Looping
        if (ls >= le) { stage = Stage::Holding; from = to = ls; return; }
        switch (mac.loopDir)
        {
            case 2:                                          // Reverse (traveling backward)
                if (at <= ls) { from = le; to = le - 1; }    // snap back to loopEnd
                else          to = at - 1;
                break;
            case 1:                                          // Ping-Pong
                if      (at >= le) to = at - 1;
                else if (at <= ls) to = at + 1;
                else               to = at + travelDir;
                break;
            default:                                         // Forward
                if (at >= le) { from = ls; to = ls + 1; }    // snap to loopStart
                else          to = at + 1;
                break;
        }
    }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/tests/VectronTests "[trajectory]"` (rebuild first)
Expected: All trajectory tests PASS, including Task 1's.

- [ ] **Step 5: Commit**

```bash
git add source/dsp/osc/VectorTrajectory.h tests/test_vector_trajectory.cpp
git commit -m "feat: trajectory loop mode — forward/reverse/ping-pong, degenerate regions"
```

---

### Task 3: Loop+Sustain release travel + `latchFrom` (decisions 7, 10)

**Files:**
- Modify: `source/dsp/osc/VectorTrajectory.h` (fill in `exitLoopForward`, `latchFrom`)
- Modify: `tests/test_vector_trajectory.cpp` (append tests)

**Interfaces:**
- Consumes: Tasks 1-2.
- Produces: `release()` on a Looping mode-3 playhead → `Stage::ReleaseTail`, traveling forward to Pn, then `Holding`. Backward travel flips with position continuity (`swap(from,to); segPhase = 1 - segPhase`). Release during Travel (mode 3) skips the loop entirely. `latchFrom(master, mac)` copies `{from, to, segPhase, stage}`, clears `released`; with `mode == 1` it forces forward Travel toward Pn.

- [ ] **Step 1: Append the failing tests**

```cpp
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
```

- [ ] **Step 2: Run tests to verify the new ones fail**

Run: `cmake --build build --target VectronTests 2>&1 | tail -3 && ./build/tests/VectronTests "[trajectory]"`
Expected: FAIL — release keeps looping; latch does nothing.

- [ ] **Step 3: Implement**

Replace the `exitLoopForward` stub with:

```cpp
    void exitLoopForward() noexcept
    {
        if (to < from)                                       // flip backward travel, keep position
        {
            const int t = to; to = from; from = t;
            segPhase = 1.0f - segPhase;
        }
        stage = Stage::ReleaseTail;
    }
```

Replace the out-of-class `latchFrom` definition with:

```cpp
inline void TrajectoryPlayhead::latchFrom (const TrajectoryPlayhead& master,
                                           const TrajectoryMacros& mac) noexcept
{
    from = master.from; to = master.to; segPhase = master.segPhase; stage = master.stage;
    released = false;
    if (mac.mode == 1)                                       // One-Shot: forward to Pn from here
    {
        if (to < from)
        {
            const int t = to; to = from; from = t;
            segPhase = 1.0f - segPhase;
        }
        if (stage != Stage::Holding) stage = Stage::Travel;
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/tests/VectronTests "[trajectory]"` (rebuild first)
Expected: All trajectory tests PASS.

- [ ] **Step 5: Commit**

```bash
git add source/dsp/osc/VectorTrajectory.h tests/test_vector_trajectory.cpp
git commit -m "feat: trajectory loop+sustain release travel and master-phase latch"
```

---

### Task 4: Smooth interpolation — cosine ease + tension Bézier bow (decision 9)

**Files:**
- Modify: `source/dsp/osc/VectorTrajectory.h` (`outputPosition`)
- Modify: `tests/test_vector_trajectory.cpp` (append tests)

**Interfaces:**
- Consumes: Tasks 1-3.
- Produces: `interp == 1` → eased `t = 0.5 − 0.5·cos(π·segPhase)`; per-point `tension` (owned by the later-indexed point) bends the segment as a quadratic Bézier whose control point sits `tension × 0.5 × chordLength` along the unit perpendicular `(-dy, dx)/len` from the chord midpoint. `interp == 0` ignores tension. Output always clamped to ±1.

- [ ] **Step 1: Append the failing tests**

```cpp
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
```

- [ ] **Step 2: Run tests to verify the new ones fail**

Run: `cmake --build build --target VectronTests 2>&1 | tail -3 && ./build/tests/VectronTests "[trajectory]"`
Expected: FAIL — smooth cases produce linear positions.

- [ ] **Step 3: Implement**

Replace `outputPosition` with:

```cpp
    void outputPosition (const TrajectoryModel& m, const TrajectoryMacros& mac,
                         float& ox, float& oy) const noexcept
    {
        const auto& a = m.points[from];
        const auto& b = m.points[to];
        float t = segPhase;
        if (mac.interp == 1)
            t = 0.5f - 0.5f * std::cos (t * 3.14159265358979f);   // cosine ease
        float x = a.x + (b.x - a.x) * t;
        float y = a.y + (b.y - a.y) * t;
        if (mac.interp == 1)
        {
            // Quadratic Bézier bow: control point sits tension * 0.5 * chordLen along the
            // unit perpendicular from the chord midpoint (0 = straight, spec decision 9).
            const float tension = m.points[from > to ? from : to].tension;
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::sqrt (dx * dx + dy * dy);
            if (tension != 0.0f && len > 1.0e-6f)
            {
                const float px = -dy / len, py = dx / len;
                const float cx = 0.5f * (a.x + b.x) + tension * 0.5f * len * px;
                const float cy = 0.5f * (a.y + b.y) + tension * 0.5f * len * py;
                const float u  = 1.0f - t;
                x = u * u * a.x + 2.0f * u * t * cx + t * t * b.x;
                y = u * u * a.y + 2.0f * u * t * cy + t * t * b.y;
            }
        }
        ox = clamp1 (x);
        oy = clamp1 (y);
    }
```

(Remove the `(void) mac;` line — `mac` is now used.)

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/tests/VectronTests "[trajectory]"` (rebuild first)
Expected: All trajectory tests PASS.

- [ ] **Step 5: Run the full unit suite and commit**

Run: `./build/tests/VectronTests | tail -3`
Expected: all pass.

```bash
git add source/dsp/osc/VectorTrajectory.h tests/test_vector_trajectory.cpp
git commit -m "feat: trajectory smooth interp — cosine ease + tension bezier bow"
```

---

### Task 5: `DstTrajDepth` matrix destination + the 11 APVTS macro params (decision 11, spec params table)

**Files:**
- Modify: `source/dsp/mod/ModMatrix.h` (append `DstTrajDepth`)
- Modify: `source/params/ParameterLayout.cpp` (append "Traj Depth" to `dstNames`; add 11 params)
- Modify: `tests/test_mod_matrix.cpp` (contract test)

**Interfaces:**
- Consumes: existing `ModMatrix::Dest` enum and `createParameterLayout()`.
- Produces: `MM::DstTrajDepth == 25`, `MM::kNumDests == 26`; APVTS params `traj_mode` (choice Off/One-Shot/Loop/Loop+Sustain), `traj_depth` (0-1, default 1), `traj_rate` (0.25-4 log, default 1), `traj_sync` (bool false), `traj_loopStart`/`traj_loopEnd` (int 0-15, defaults 0/3), `traj_loopDir` (choice Forward/Ping-Pong/Reverse), `traj_interp` (choice Linear/Smooth), `traj_trigger` (choice Per-Note/Global), `traj_retrigger` (bool true), `traj_recPoints` (int 4-16, default 8).

- [ ] **Step 1: Write the failing contract test**

In `tests/test_mod_matrix.cpp`, update the first test case to:

```cpp
TEST_CASE ("enum sizes match the PRD contract")
{
    STATIC_REQUIRE (MM::kNumSources == 11);
    STATIC_REQUIRE (MM::kNumDests  == 26);          // PRD §6.3's 25 + Traj Depth (Phase 6 spec)
    STATIC_REQUIRE (MM::kNumSlots  == 8);
    STATIC_REQUIRE (MM::DstTrajDepth == 25);        // appended last: existing preset indices hold
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target VectronTests 2>&1 | tail -5`
Expected: compile FAIL — `DstTrajDepth` undeclared / `STATIC_REQUIRE` fails.

- [ ] **Step 3: Implement**

In `source/dsp/mod/ModMatrix.h`, change the last line of the `Dest` enum from:

```cpp
        DstLfo1Rate, DstLfo2Rate, DstPan,
        kNumDests
```

to:

```cpp
        DstLfo1Rate, DstLfo2Rate, DstPan,
        DstTrajDepth,                       // Phase 6: appended last so preset indices hold
        kNumDests
```

In `source/params/ParameterLayout.cpp`, append `"Traj Depth"` to `dstNames`:

```cpp
                                                "LFO1 Rate", "LFO2 Rate", "Pan",
                                                "Traj Depth" };
```

(the existing `static_assert (std::size (dstNames) == (size_t) ModMatrix::kNumDests);` now enforces it), and add before `return layout;`:

```cpp
        // --- Phase 6: vector trajectory macro-controls (PRD §5.2.1) ---
        // Point data is NOT parameterised — it lives in the "TRAJECTORY" ValueTree
        // child of the APVTS state (spec decision 2).
        layout.add (std::make_unique<APC> (juce::ParameterID { "traj_mode", 1 },
            "Traj Mode", juce::StringArray { "Off", "One-Shot", "Loop", "Loop+Sustain" }, 0));
        layout.add (std::make_unique<APF> (juce::ParameterID { "traj_depth", 1 },
            "Traj Depth", juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "traj_rate", 1 },
            "Traj Rate", logRange (0.25f, 4.0f), 1.0f));
        layout.add (std::make_unique<APB> (juce::ParameterID { "traj_sync", 1 },
            "Traj Sync", false));
        layout.add (std::make_unique<API> (juce::ParameterID { "traj_loopStart", 1 },
            "Traj Loop Start", 0, 15, 0));
        layout.add (std::make_unique<API> (juce::ParameterID { "traj_loopEnd", 1 },
            "Traj Loop End", 0, 15, 3));
        layout.add (std::make_unique<APC> (juce::ParameterID { "traj_loopDir", 1 },
            "Traj Loop Dir", juce::StringArray { "Forward", "Ping-Pong", "Reverse" }, 0));
        layout.add (std::make_unique<APC> (juce::ParameterID { "traj_interp", 1 },
            "Traj Interp", juce::StringArray { "Linear", "Smooth" }, 0));
        layout.add (std::make_unique<APC> (juce::ParameterID { "traj_trigger", 1 },
            "Traj Trigger", juce::StringArray { "Per-Note", "Global" }, 0));
        layout.add (std::make_unique<APB> (juce::ParameterID { "traj_retrigger", 1 },
            "Traj Retrigger", true));
        layout.add (std::make_unique<API> (juce::ParameterID { "traj_recPoints", 1 },
            "Traj Rec Points", 4, 16, 8));   // dormant until the GUI recording phase
```

- [ ] **Step 4: Build everything and run all tests**

Run: `cmake --build build 2>&1 | tail -3 && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: full build green; all tests (unit + smoke) pass.

- [ ] **Step 5: Commit**

```bash
git add source/dsp/mod/ModMatrix.h source/params/ParameterLayout.cpp tests/test_mod_matrix.cpp
git commit -m "feat: Phase 6 params — trajectory macros + Traj Depth matrix destination"
```

---

### Task 6: `TrajectoryState.h` — ValueTree ↔ model + default path (decisions 2, 5, 12)

**Files:**
- Create: `source/params/TrajectoryState.h`
- Modify: `tests/smoke_main.cpp` (state-helper checks; runs without any processor wiring)

**Interfaces:**
- Consumes: `TrajectoryModel` (Task 1).
- Produces: `vectron::traj_ids::{tree, point, x, y, timeMs, beats, tension}` Identifiers; `juce::ValueTree vectron::createDefaultTrajectory()` (4 corner points A→B→D→C, 500 ms / 1 beat, tension 0); `vectron::TrajectoryModel vectron::trajectoryFromState (const juce::ValueTree&)` (clamps x/y to ±1, timeMs to [1, 10000], beats to [0.0625, 64], tension to ±1; missing properties default; caps at 16 points; invalid tree → `numPoints == 0`).

- [ ] **Step 1: Write the failing smoke check**

In `tests/smoke_main.cpp`, add to the includes:

```cpp
#include "params/TrajectoryState.h"
```

add this function to the anonymous namespace:

```cpp
    bool trajectoryStateHelpersOk()
    {
        const auto tree = vectron::createDefaultTrajectory();
        const auto m = vectron::trajectoryFromState (tree);
        if (m.numPoints != 4)
        { std::printf ("FAIL: default trajectory has %d points\n", m.numPoints); return false; }
        const float ex[4] { -1.0f, 1.0f, 1.0f, -1.0f };
        const float ey[4] {  1.0f, 1.0f, -1.0f, -1.0f };
        for (int i = 0; i < 4; ++i)
            if (m.points[i].x != ex[i] || m.points[i].y != ey[i] || m.points[i].timeMs != 500.0f)
            { std::printf ("FAIL: default trajectory point %d wrong\n", i); return false; }

        // Out-of-range values clamp; missing properties fall back to defaults.
        juce::ValueTree t (vectron::traj_ids::tree);
        juce::ValueTree p (vectron::traj_ids::point);
        p.setProperty (vectron::traj_ids::x, 5.0f, nullptr);
        p.setProperty (vectron::traj_ids::timeMs, 0.0f, nullptr);
        t.appendChild (p, nullptr);
        t.appendChild (juce::ValueTree (vectron::traj_ids::point), nullptr);
        const auto m2 = vectron::trajectoryFromState (t);
        if (m2.numPoints != 2 || m2.points[0].x != 1.0f || m2.points[0].timeMs != 1.0f
            || m2.points[1].timeMs != 500.0f)
        { std::printf ("FAIL: trajectory clamping/defaults wrong\n"); return false; }

        // More than 16 POINT children cap at 16; an invalid tree parses to 0 points.
        juce::ValueTree big (vectron::traj_ids::tree);
        for (int i = 0; i < 20; ++i)
            big.appendChild (juce::ValueTree (vectron::traj_ids::point), nullptr);
        if (vectron::trajectoryFromState (big).numPoints != 16)
        { std::printf ("FAIL: trajectory point cap wrong\n"); return false; }
        if (vectron::trajectoryFromState (juce::ValueTree()).numPoints != 0)
        { std::printf ("FAIL: invalid tree should parse to 0 points\n"); return false; }

        std::printf ("ok: trajectory state helpers\n");
        return true;
    }
```

and at the top of `main` (right after `juce::ScopedJuceInitialiser_GUI juceInit;`):

```cpp
    if (! trajectoryStateHelpersOk()) return 1;
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target VectronSmoke 2>&1 | tail -5`
Expected: compile FAIL — `params/TrajectoryState.h` not found.

- [ ] **Step 3: Implement**

Create `source/params/TrajectoryState.h`:

```cpp
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "dsp/osc/VectorTrajectory.h"

// ValueTree <-> TrajectoryModel bridge (spec decision 2): trajectory points are
// state, not params. The "TRAJECTORY" tree is a child of the APVTS state and
// round-trips through the existing XML preset serialization.
namespace vectron
{
namespace traj_ids
{
    static const juce::Identifier tree    ("TRAJECTORY");
    static const juce::Identifier point   ("POINT");
    static const juce::Identifier x       ("x");
    static const juce::Identifier y       ("y");
    static const juce::Identifier timeMs  ("timeMs");
    static const juce::Identifier beats   ("beats");
    static const juce::Identifier tension ("tension");
}

// Default path = the four oscillator corners A(-1,+1) -> B(+1,+1) -> D(+1,-1) -> C(-1,-1),
// 500 ms / 1 beat per segment (spec decision 5) — audible as soon as traj_mode leaves Off.
inline juce::ValueTree createDefaultTrajectory()
{
    juce::ValueTree t (traj_ids::tree);
    const float xs[4] { -1.0f, 1.0f, 1.0f, -1.0f };
    const float ys[4] {  1.0f, 1.0f, -1.0f, -1.0f };
    for (int i = 0; i < 4; ++i)
    {
        juce::ValueTree p (traj_ids::point);
        p.setProperty (traj_ids::x, xs[i], nullptr);
        p.setProperty (traj_ids::y, ys[i], nullptr);
        p.setProperty (traj_ids::timeMs, 500.0f, nullptr);
        p.setProperty (traj_ids::beats, 1.0f, nullptr);
        p.setProperty (traj_ids::tension, 0.0f, nullptr);
        t.appendChild (p, nullptr);
    }
    return t;
}

inline TrajectoryModel trajectoryFromState (const juce::ValueTree& t)
{
    TrajectoryModel m;
    if (! t.isValid()) return m;
    for (int i = 0; i < t.getNumChildren() && m.numPoints < TrajectoryModel::kMaxPoints; ++i)
    {
        const auto c = t.getChild (i);
        if (! c.hasType (traj_ids::point)) continue;
        auto& p = m.points[m.numPoints++];
        p.x       = juce::jlimit (-1.0f, 1.0f,      (float) c.getProperty (traj_ids::x, 0.0f));
        p.y       = juce::jlimit (-1.0f, 1.0f,      (float) c.getProperty (traj_ids::y, 0.0f));
        p.timeMs  = juce::jlimit (1.0f, 10000.0f,   (float) c.getProperty (traj_ids::timeMs, 500.0f));
        p.beats   = juce::jlimit (0.0625f, 64.0f,   (float) c.getProperty (traj_ids::beats, 1.0f));
        p.tension = juce::jlimit (-1.0f, 1.0f,      (float) c.getProperty (traj_ids::tension, 0.0f));
    }
    return m;
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target VectronSmoke 2>&1 | tail -3 && ./build/tests/VectronSmoke_artefacts/Debug/VectronSmoke`
Expected: `ok: trajectory state helpers` followed by the existing checks and `SMOKE OK`.

- [ ] **Step 5: Commit**

```bash
git add source/params/TrajectoryState.h tests/smoke_main.cpp
git commit -m "feat: trajectory ValueTree state bridge + default corner path"
```

---

### Task 7: Voice wiring — playhead, smoothing, unified blend formula (decisions 1, 7, 10, 11)

**Files:**
- Modify: `source/dsp/VectronVoice.h`
- Modify: `source/dsp/VectronVoice.cpp`

**Interfaces:**
- Consumes: `TrajectoryModel`, `TrajectoryMacros`, `TrajectoryPlayhead` (Tasks 1-4), `MM::DstTrajDepth` (Task 5).
- Produces: `VectronVoiceParams` fields the processor fills in Task 8 — `trajMode, trajDepth, trajRate, trajSync, trajSecondsPerBeat, trajLoopStart, trajLoopEnd, trajLoopDir, trajInterp, trajGlobal, trajRetrigger` plus `const vectron::TrajectoryModel* trajModel`, `vectron::TrajectoryPlayhead trajMasterState`, `float trajMasterX, trajMasterY`. Voice is a no-op while `trajModel == nullptr` or `trajMode == 0`.

- [ ] **Step 1: Extend `VectronVoiceParams` and the voice members**

`VectronVoice.h` already includes `"osc/VectorEngine.h"`; add below it:

```cpp
#include "osc/VectorTrajectory.h"
```

Append to `struct VectronVoiceParams` (after the `slots` member):

```cpp
    // Phase 6: vector trajectory (macros; points come via the trajModel snapshot pointer)
    int   trajMode           { 0 };      // 0 Off, 1 One-Shot, 2 Loop, 3 Loop+Sustain
    float trajDepth          { 1.0f };
    float trajRate           { 1.0f };
    bool  trajSync           { false };
    float trajSecondsPerBeat { 0.5f };   // 60/bpm, processor-resolved (voice never sees BPM)
    int   trajLoopStart      { 0 };
    int   trajLoopEnd        { 3 };
    int   trajLoopDir        { 0 };
    int   trajInterp         { 0 };
    bool  trajGlobal         { false };  // traj_trigger == Global
    bool  trajRetrigger      { true };
    const vectron::TrajectoryModel* trajModel { nullptr };   // processor-owned, block-stable
    vectron::TrajectoryPlayhead trajMasterState;             // master playhead at block start
    float trajMasterX { 0.0f }, trajMasterY { 0.0f };        // master position at block start
```

Add to the private members of `VectronVoice` (after `VectronVoiceParams params;`):

```cpp
    vectron::TrajectoryPlayhead trajPlayhead;
    juce::SmoothedValue<float> trajX, trajY;     // block-rate playhead, smoothed per sample
    double voiceSampleRate = 48000.0;
```

and a private helper declaration next to `applyParams()`:

```cpp
    vectron::TrajectoryMacros trajMacros() const noexcept;
```

- [ ] **Step 2: Wire prepare / startNote / stopNote / render in `VectronVoice.cpp`**

In `prepare`, after the `ccSmoothCoef` line:

```cpp
    voiceSampleRate = sampleRate;
    trajX.reset (sampleRate, 0.02);
    trajY.reset (sampleRate, 0.02);
```

Add the helper (below `applyParams`):

```cpp
vectron::TrajectoryMacros VectronVoice::trajMacros() const noexcept
{
    vectron::TrajectoryMacros m;
    m.mode           = params.trajMode;
    m.rate           = params.trajRate;
    m.sync           = params.trajSync;
    m.secondsPerBeat = params.trajSecondsPerBeat;
    m.loopStart      = params.trajLoopStart;
    m.loopEnd        = params.trajLoopEnd;
    m.loopDir        = params.trajLoopDir;
    m.interp         = params.trajInterp;
    return m;
}
```

In `startNote`, just before `level = velocity;`:

```cpp
    if (params.trajMode != 0 && params.trajModel != nullptr)
    {
        const auto mac = trajMacros();
        if (params.trajGlobal)
        {
            trajX.setCurrentAndTargetValue (params.trajMasterX);
            trajY.setCurrentAndTargetValue (params.trajMasterY);
        }
        else
        {
            if (params.trajRetrigger) trajPlayhead.noteOn (*params.trajModel, mac);
            else                      trajPlayhead.latchFrom (params.trajMasterState, mac);
            float tx = 0.0f, ty = 0.0f;
            trajPlayhead.advance (*params.trajModel, mac, 0.0f, tx, ty);
            trajX.setCurrentAndTargetValue (tx);
            trajY.setCurrentAndTargetValue (ty);
        }
    }
```

In `stopNote`, inside the `if (allowTailOff)` branch, add:

```cpp
        trajPlayhead.release();
```

In `renderNextBlock`, after the Global-mode LFO phase loop and before `const float keytrackSrc = ...`:

```cpp
    // Phase 6: advance the per-voice trajectory once per block (control rate, spec
    // decision 1), smoothed per sample. Global trigger follows the master playhead.
    if (params.trajMode != 0 && params.trajModel != nullptr)
    {
        if (params.trajGlobal)
        {
            trajX.setTargetValue (params.trajMasterX);
            trajY.setTargetValue (params.trajMasterY);
        }
        else
        {
            float tx = 0.0f, ty = 0.0f;
            trajPlayhead.advance (*params.trajModel, trajMacros(),
                                  (float) (numSamples / voiceSampleRate), tx, ty);
            trajX.setTargetValue (tx);
            trajY.setTargetValue (ty);
        }
    }
```

Replace the vector-position lines:

```cpp
        const float lx = lfo[0].processSample();                 // Phase 2 axis LFOs
        const float ly = lfo[1].processSample();
        const float fx = juce::jlimit (-1.0f, 1.0f, baseX.getNextValue() + lx + dest[MM::DstVectorX]);
        const float fy = juce::jlimit (-1.0f, 1.0f, baseY.getNextValue() + ly + dest[MM::DstVectorY]);
        engine.setVectorPosition (fx, fy);
```

with:

```cpp
        const float lx = lfo[0].processSample();                 // Phase 2 axis LFOs
        const float ly = lfo[1].processSample();
        // Phase 6 unified vector model (PRD §5.2): static<->trajectory blend, LFOs
        // and matrix offsets always on top. Off mode == Phase 5 behavior exactly.
        const float tjx   = trajX.getNextValue();
        const float tjy   = trajY.getNextValue();
        const float depth = params.trajMode == 0 ? 0.0f
                          : juce::jlimit (0.0f, 1.0f, params.trajDepth + dest[MM::DstTrajDepth]);
        const float fx = juce::jlimit (-1.0f, 1.0f,
            baseX.getNextValue() * (1.0f - depth) + tjx * depth + lx + dest[MM::DstVectorX]);
        const float fy = juce::jlimit (-1.0f, 1.0f,
            baseY.getNextValue() * (1.0f - depth) + tjy * depth + ly + dest[MM::DstVectorY]);
        engine.setVectorPosition (fx, fy);
```

- [ ] **Step 3: Build everything and run all tests (no regressions)**

Run: `cmake --build build 2>&1 | tail -3 && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: build green; all tests pass (trajectory is inert — `trajModel` is still always `nullptr`).

- [ ] **Step 4: Commit**

```bash
git add source/dsp/VectronVoice.h source/dsp/VectronVoice.cpp
git commit -m "feat: voice trajectory wiring — playhead, smoothing, unified vector blend"
```

---

### Task 8: Processor wiring — params, listener, snapshot, master playhead (decisions 3, 6, 10)

**Files:**
- Modify: `source/PluginProcessor.h`
- Modify: `source/PluginProcessor.cpp`

**Interfaces:**
- Consumes: Task 5 param IDs, Task 6 state helpers, Task 7 `VectronVoiceParams` fields.
- Produces: a running system — `"TRAJECTORY"` state child auto-created, message-thread edits reach the audio thread via SpinLock try-lock + `trajVersion`, master playhead advanced per block in forced-Loop semantics, all macros + model pointer pushed to voices.

- [ ] **Step 1: Extend the header**

In `PluginProcessor.h`, add after the existing includes:

```cpp
#include "dsp/osc/VectorTrajectory.h"
```

Change the class declaration to add the listener base:

```cpp
class VectronProcessor : public juce::AudioProcessor, private juce::ValueTree::Listener
```

Add to the private section (after `double masterLfoPhase[2] { 0.0, 0.0 };`):

```cpp
    // Phase 6: trajectory params
    std::atomic<float>* pTrajMode      { nullptr };
    std::atomic<float>* pTrajDepth     { nullptr };
    std::atomic<float>* pTrajRate      { nullptr };
    std::atomic<float>* pTrajSync      { nullptr };
    std::atomic<float>* pTrajLoopStart { nullptr };
    std::atomic<float>* pTrajLoopEnd   { nullptr };
    std::atomic<float>* pTrajLoopDir   { nullptr };
    std::atomic<float>* pTrajInterp    { nullptr };
    std::atomic<float>* pTrajTrigger   { nullptr };
    std::atomic<float>* pTrajRetrigger { nullptr };
    std::atomic<float>* pTrajRecPoints { nullptr };

    // Phase 6: trajectory model handoff (spec decision 3). Message thread rewrites
    // sharedTrajModel under trajLock and bumps trajVersion; the audio thread TRY-locks
    // on version change and copies into audioTrajModel, keeping last-good on failure.
    vectron::TrajectoryModel sharedTrajModel;
    juce::SpinLock trajLock;
    std::atomic<int> trajVersion { 1 };
    vectron::TrajectoryModel audioTrajModel;
    int  audioTrajVersion  = 0;
    vectron::TrajectoryPlayhead masterTraj;      // free-running, always Loop semantics
    bool masterTrajStarted = false;

    void ensureTrajectoryState();
    void refreshTrajectoryModel();
    void maybeRefreshTrajectory (const juce::ValueTree& changed);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override {}
    void valueTreeRedirected (juce::ValueTree&) override;
```

- [ ] **Step 2: Implement in `PluginProcessor.cpp`**

Add to the includes:

```cpp
#include "params/TrajectoryState.h"
```

In the constructor, after the `mod` slot pointer loop and before the `jassert` block, add:

```cpp
    pTrajMode      = apvts.getRawParameterValue ("traj_mode");
    pTrajDepth     = apvts.getRawParameterValue ("traj_depth");
    pTrajRate      = apvts.getRawParameterValue ("traj_rate");
    pTrajSync      = apvts.getRawParameterValue ("traj_sync");
    pTrajLoopStart = apvts.getRawParameterValue ("traj_loopStart");
    pTrajLoopEnd   = apvts.getRawParameterValue ("traj_loopEnd");
    pTrajLoopDir   = apvts.getRawParameterValue ("traj_loopDir");
    pTrajInterp    = apvts.getRawParameterValue ("traj_interp");
    pTrajTrigger   = apvts.getRawParameterValue ("traj_trigger");
    pTrajRetrigger = apvts.getRawParameterValue ("traj_retrigger");
    pTrajRecPoints = apvts.getRawParameterValue ("traj_recPoints");
```

extend the `jassert` block with:

```cpp
    jassert (pTrajMode && pTrajDepth && pTrajRate && pTrajSync && pTrajLoopStart
             && pTrajLoopEnd && pTrajLoopDir && pTrajInterp && pTrajTrigger
             && pTrajRetrigger && pTrajRecPoints);
```

and at the very end of the constructor:

```cpp
    ensureTrajectoryState();
    apvts.state.addListener (this);   // survives replaceState via valueTreeRedirected
```

Add the new member functions (before `prepareToPlay`):

```cpp
void VectronProcessor::ensureTrajectoryState()
{
    if (! apvts.state.getChildWithName (vectron::traj_ids::tree).isValid())
        apvts.state.appendChild (vectron::createDefaultTrajectory(), nullptr);
    refreshTrajectoryModel();
}

void VectronProcessor::refreshTrajectoryModel()
{
    // Message thread: parse outside the lock, hold the SpinLock only for the copy.
    const auto model = vectron::trajectoryFromState (
        apvts.state.getChildWithName (vectron::traj_ids::tree));
    {
        const juce::SpinLock::ScopedLockType sl (trajLock);
        sharedTrajModel = model;
    }
    trajVersion.fetch_add (1);
}

void VectronProcessor::maybeRefreshTrajectory (const juce::ValueTree& changed)
{
    if (changed.hasType (vectron::traj_ids::tree) || changed.hasType (vectron::traj_ids::point))
        refreshTrajectoryModel();
}

void VectronProcessor::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&)
{
    maybeRefreshTrajectory (t);
}
void VectronProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    maybeRefreshTrajectory (parent);
}
void VectronProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    maybeRefreshTrajectory (parent);
}
void VectronProcessor::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    maybeRefreshTrajectory (parent);
}
void VectronProcessor::valueTreeRedirected (juce::ValueTree&)
{
    ensureTrajectoryState();          // replaceState swapped the root: re-create + re-parse
}
```

In `prepareToPlay`, add:

```cpp
    masterTrajStarted = false;
```

In `setStateInformation`, add a defensive re-ensure after `replaceState` (the redirect callback normally covers it):

```cpp
void VectronProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    ensureTrajectoryState();
}
```

In `processBlock`, after the Phase 5 mod-LFO loop (which defines `bpm` and `sr`) and before `vp.modVelAmt = ...`, add:

```cpp
    // Phase 6: trajectory — refresh audio snapshot, advance master playhead, resolve macros.
    if (audioTrajVersion != trajVersion.load())
    {
        const juce::SpinLock::ScopedTryLockType tl (trajLock);   // never blocks the audio thread
        if (tl.isLocked())
        {
            audioTrajModel   = sharedTrajModel;
            audioTrajVersion = trajVersion.load();
        }
    }
    vp.trajMode           = (int) pTrajMode->load();
    vp.trajDepth          =       pTrajDepth->load();
    vp.trajRate           =       pTrajRate->load();
    vp.trajSync           =       pTrajSync->load() > 0.5f;
    vp.trajSecondsPerBeat = (float) (60.0 / bpm);
    vp.trajLoopStart      = (int) pTrajLoopStart->load();
    vp.trajLoopEnd        = (int) pTrajLoopEnd->load();
    vp.trajLoopDir        = (int) pTrajLoopDir->load();
    vp.trajInterp         = (int) pTrajInterp->load();
    vp.trajGlobal         =       pTrajTrigger->load() > 0.5f;
    vp.trajRetrigger      =       pTrajRetrigger->load() > 0.5f;
    vp.trajModel          = &audioTrajModel;

    if (vp.trajMode == 0)
        masterTrajStarted = false;               // re-arm so enabling restarts from P0
    else
    {
        vectron::TrajectoryMacros mac;
        mac.mode           = 2;                  // master always free-runs Loop (spec decision 10)
        mac.rate           = vp.trajRate;
        mac.sync           = vp.trajSync;
        mac.secondsPerBeat = vp.trajSecondsPerBeat;
        mac.loopStart      = vp.trajLoopStart;
        mac.loopEnd        = vp.trajLoopEnd;
        mac.loopDir        = vp.trajLoopDir;
        mac.interp         = vp.trajInterp;
        if (! masterTrajStarted)
        {
            masterTraj.noteOn (audioTrajModel, mac);
            masterTrajStarted = true;
        }
        float mx = 0.0f, my = 0.0f;
        masterTraj.advance (audioTrajModel, mac, 0.0f, mx, my);  // block-start position
        vp.trajMasterState = masterTraj;
        vp.trajMasterX     = mx;
        vp.trajMasterY     = my;
        if (sr > 0.0)
            masterTraj.advance (audioTrajModel, mac,
                                (float) (buffer.getNumSamples() / sr), mx, my);
    }
```

- [ ] **Step 3: Build everything and run all tests**

Run: `cmake --build build 2>&1 | tail -3 && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: build green; all tests pass (smoke still exercises the default Off mode + state helpers).

- [ ] **Step 4: Commit**

```bash
git add source/PluginProcessor.h source/PluginProcessor.cpp
git commit -m "feat: processor trajectory wiring — state listener, snapshot handoff, master playhead"
```

---

### Task 9: Smoke acceptance — the trajectory is audible end-to-end (PRD §12.6, spec Testing)

**Files:**
- Modify: `tests/smoke_main.cpp`

**Interfaces:**
- Consumes: the fully wired system (Tasks 5-8). Matrix `Traj Depth` destination index is 25; `traj_mode` choice index 2 = Loop, 1 = One-Shot.
- Produces: four acceptance checks in the existing exit-code harness.

- [ ] **Step 1: Add a windowed diff helper**

In the anonymous namespace of `tests/smoke_main.cpp`, next to `rmsDifference`:

```cpp
    float rmsDifferenceWindow (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                               int start, int len)
    {
        double acc = 0.0;
        for (int i = start; i < start + len; ++i)
        {
            const double d = (double) a.getSample (0, i) - (double) b.getSample (0, i);
            acc += d * d;
        }
        return (float) std::sqrt (acc / len);
    }
```

- [ ] **Step 2: Add the acceptance checks**

In `main`, after the existing "Routing 3" block and before the final `SMOKE OK`, add:

```cpp
    // Trajectory 1 (acceptance, PRD §12.6): Loop mode audibly moves the vector.
    juce::AudioBuffer<float> loopBuf;
    {
        VectronProcessor p;
        setParam (p, "traj_mode", 2.0f);               // Loop
        render (p, loopBuf);
        if (! finiteAndAudible (loopBuf, "traj loop")) return 1;
        const float diff = rmsDifference (loopBuf, baseBuf);
        std::printf ("traj loop diff rms = %.5f\n", diff);
        if (diff < 1.0e-3f) { std::printf ("FAIL: trajectory Loop has no audible effect\n"); return 1; }
    }

    // Trajectory 2: One-Shot settles and holds at Pn == corner C (-1,-1).
    {
        VectronProcessor p;
        setParam (p, "traj_mode", 1.0f);               // One-Shot
        setParam (p, "traj_rate", 4.0f);               // whole path done in ~375 ms
        juce::AudioBuffer<float> oneShot;
        render (p, oneShot);

        VectronProcessor ref;                          // static patch parked at Pn
        setParam (ref, "vector_x", -1.0f);
        setParam (ref, "vector_y", -1.0f);
        juce::AudioBuffer<float> refBuf;
        render (ref, refBuf);

        const int start = (int) kSampleRate;           // 1 s: well past the path + smoothing
        const int len   = oneShot.getNumSamples() - start;
        const float diff = rmsDifferenceWindow (oneShot, refBuf, start, len);
        std::printf ("traj one-shot settle diff rms = %.5f\n", diff);
        if (diff > 1.0e-3f) { std::printf ("FAIL: One-Shot does not settle at Pn\n"); return 1; }
    }

    // Trajectory 3: matrix LFO1 -> Traj Depth fades the trajectory in and out.
    {
        VectronProcessor still;
        setParam (still, "traj_mode", 2.0f);
        setParam (still, "traj_depth", 0.0f);          // parked until modulated
        juce::AudioBuffer<float> stillBuf;
        render (still, stillBuf);

        VectronProcessor p;
        setParam (p, "traj_mode", 2.0f);
        setParam (p, "traj_depth", 0.0f);
        setParam (p, "lfo1_rate", 5.0f);
        setParam (p, "mod1_src", 0.0f);                // LFO 1
        setParam (p, "mod1_dst", 25.0f);               // Traj Depth
        setParam (p, "mod1_amt", 1.0f);
        setParam (p, "mod1_en",  1.0f);
        juce::AudioBuffer<float> buf;
        render (p, buf);
        const float diff = rmsDifference (buf, stillBuf);
        std::printf ("lfo->trajDepth diff rms = %.5f\n", diff);
        if (diff < 1.0e-3f) { std::printf ("FAIL: LFO->Traj Depth routing has no audible effect\n"); return 1; }
    }

    // Trajectory 4: editing a point in the state is heard (listener -> snapshot path).
    {
        VectronProcessor p;
        setParam (p, "traj_mode", 2.0f);
        auto traj = p.apvts.state.getChildWithName (vectron::traj_ids::tree);
        if (! traj.isValid() || traj.getNumChildren() < 2)
        { std::printf ("FAIL: TRAJECTORY state child missing\n"); return 1; }
        traj.getChild (1).setProperty (vectron::traj_ids::x, -1.0f, nullptr);   // B -> top-left
        juce::AudioBuffer<float> buf;
        render (p, buf);
        const float diff = rmsDifference (buf, loopBuf);
        std::printf ("traj point-edit diff rms = %.5f\n", diff);
        if (diff < 1.0e-3f) { std::printf ("FAIL: editing a trajectory point has no audible effect\n"); return 1; }
    }
```

- [ ] **Step 3: Build and run the smoke harness**

Run: `cmake --build build --target VectronSmoke 2>&1 | tail -3 && ./build/tests/VectronSmoke_artefacts/Debug/VectronSmoke`
Expected: all checks print `ok`/diff lines and it ends with `SMOKE OK`. These are acceptance tests for already-implemented behavior — if any fails, debug the wiring (Tasks 7-8), not the thresholds.

- [ ] **Step 4: Run the entire suite**

Run: `cmake --build build 2>&1 | tail -3 && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 100% tests passed (VectronTests + VectronSmoke).

- [ ] **Step 5: Commit**

```bash
git add tests/smoke_main.cpp
git commit -m "test: trajectory acceptance smoke — loop audibility, one-shot settle, depth mod, state edits"
```

---

## After the plan

Per the established phase workflow: subagent code review of the branch, fix findings, update the spec with any review-driven decisions, fast-forward merge `feat/phase-6-trajectory` to `main`, push, delete the branch.
