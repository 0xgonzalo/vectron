#pragma once
#include <cmath>
#include <algorithm>

// TPT / Zavalishin state-variable filter (Andrew Simper topology).
// One input -> LP/BP/HP/Notch. Unconditionally stable. JUCE-free.
class SvfFilter
{
public:
    enum class Mode { LP, BP, HP, Notch };

    SvfFilter() noexcept { update(); }

    void setSampleRate (double sr) noexcept { sampleRate = sr; update(); }
    void setCutoff (float hz)      noexcept { cutoff = hz; update(); }
    void setResonance (float r)    noexcept { res = std::clamp (r, 0.0f, 1.0f); update(); }
    void setMode (Mode m)          noexcept { mode = m; }

    void reset() noexcept { ic1eq = 0.0f; ic2eq = 0.0f; }

    float processSample (float v0) noexcept
    {
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        switch (mode)
        {
            case Mode::LP:    return v2;
            case Mode::BP:    return v1;
            case Mode::HP:    return v0 - k * v1 - v2;
            case Mode::Notch: return v0 - k * v1;
        }
        return v2;
    }

private:
    void update() noexcept
    {
        if (sampleRate <= 0.0) return;
        const float nyq = static_cast<float> (sampleRate * 0.5);
        const float fc  = std::clamp (cutoff, 20.0f, 0.99f * nyq);
        const float g   = std::tan (3.14159265358979f * fc / static_cast<float> (sampleRate));
        // res 0 -> k = 2 (Q ~ 0.5), res 1 -> k = 0.02 (high Q).
        k  = 2.0f - 1.98f * res;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double sampleRate = 44100.0;
    float  cutoff = 1000.0f;
    float  res    = 0.0f;
    Mode   mode   = Mode::LP;

    float k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1eq = 0.0f, ic2eq = 0.0f;
};
