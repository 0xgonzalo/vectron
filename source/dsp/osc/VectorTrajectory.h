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
    void exitLoopForward() noexcept
    {
        if (to < from)                                       // flip backward travel, keep position
        {
            const int t = to; to = from; from = t;
            segPhase = 1.0f - segPhase;
        }
        stage = Stage::ReleaseTail;
    }

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
            // Chord taken in point-index order so the path shape is invariant to traversal
            // direction — Reverse/Ping-Pong must not mirror the curve.
            const auto& lo = m.points[from < to ? from : to];
            const auto& hi = m.points[from > to ? from : to];
            const float tension = hi.tension;
            const float dx = hi.x - lo.x, dy = hi.y - lo.y;
            const float len = std::sqrt (dx * dx + dy * dy);
            if (tension != 0.0f && len > 1.0e-6f)
            {
                const float px = -dy / len, py = dx / len;
                const float cx = 0.5f * (lo.x + hi.x) + tension * 0.5f * len * px;
                const float cy = 0.5f * (lo.y + hi.y) + tension * 0.5f * len * py;
                const float u  = 1.0f - t;
                x = u * u * a.x + 2.0f * u * t * cx + t * t * b.x;
                y = u * u * a.y + 2.0f * u * t * cy + t * t * b.y;
            }
        }
        ox = clamp1 (x);
        oy = clamp1 (y);
    }

    int   from = 0, to = 0;
    float segPhase = 0.0f;
    Stage stage = Stage::Idle;
    bool  released = false;
};

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
}
