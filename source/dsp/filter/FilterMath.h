#pragma once
#include <cmath>
#include <algorithm>

namespace vectron
{
    // Effective filter cutoff (PRD §5.6): base * keytrack * envelope, clamped to
    // the audio range. keytrackPct in [-100, 100], reference note = MIDI 60.
    // env = Filter ADSR (0..1) already scaled by velocity; envAmount in [-1, 1]
    // maps to +/-5 octaves at full amount (locked in the Phase 4 design spec).
    // modOct: extra octaves from the mod matrix (Phase 5), 0 when unrouted.
    inline float effectiveCutoffHz (float baseHz, int midiNote, float keytrackPct,
                                    float env, float envAmount, float modOct = 0.0f) noexcept
    {
        constexpr float kEnvOctaves = 5.0f;
        const float keyOct = (keytrackPct * 0.01f) * (float) (midiNote - 60) / 12.0f;
        const float envOct = env * envAmount * kEnvOctaves;
        return std::clamp (baseHz * std::exp2 (keyOct + envOct + modOct), 20.0f, 20000.0f);
    }
}
