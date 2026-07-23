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
