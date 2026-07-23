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
