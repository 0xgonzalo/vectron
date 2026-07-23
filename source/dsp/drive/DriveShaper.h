#pragma once
#include <cmath>
#include <algorithm>

// Waveshaper for the voice mixer stage: Tanh / Hard clip / Foldback (PRD §5.5).
// Stateless, JUCE-free. Amount 0 is an exact identity (trim still applies) so a
// fresh patch nulls against the un-driven signal path. Pre-gain = 1 + 19*amount.
class DriveShaper
{
public:
    enum class Type { Tanh, Hard, Foldback };

    void setType (Type t)        noexcept { type = t; }
    void setAmount (float a)     noexcept { amount = std::clamp (a, 0.0f, 1.0f); }
    void setTrimDb (float db)    noexcept { trimGain = std::pow (10.0f, db * 0.05f); }
    void setTrimGain (float g)   noexcept { trimGain = g; }

    float processSample (float x) const noexcept
    {
        if (amount <= 0.0f)
            return x * trimGain;

        float y = (1.0f + 19.0f * amount) * x;
        switch (type)
        {
            case Type::Tanh:     y = std::tanh (y);               break;
            case Type::Hard:     y = std::clamp (y, -1.0f, 1.0f); break;
            case Type::Foldback: y = foldback (y);                break;
        }
        // Continuity at amount -> 0: crossfade dry->shaped over the first 4% of
        // the range so automating drive from zero cannot step the output.
        const float w = std::min (amount * 25.0f, 1.0f);
        return ((1.0f - w) * x + w * y) * trimGain;
    }

private:
    // Triangle-fold into [-1, 1]: identity on [-1, 1], reflects off the
    // boundaries beyond it (period-4 triangle through the origin).
    static float foldback (float x) noexcept
    {
        const float t    = (x + 1.0f) * 0.25f;
        const float frac = t - std::floor (t);                       // [0, 1)
        return frac < 0.5f ? frac * 4.0f - 1.0f : 3.0f - frac * 4.0f;
    }

    Type  type     = Type::Tanh;
    float amount   = 0.0f;
    float trimGain = 1.0f;
};
