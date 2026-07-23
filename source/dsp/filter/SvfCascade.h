#pragma once
#include <cmath>
#include <algorithm>
#include "SvfFilter.h"

// 12/24 dB state-variable filter: one or two SvfFilters in series (identical
// settings, incl. Notch x2), with optional tanh pre-saturation for the
// filter_drive param (PRD §5.6 "SVF pre-gain"). JUCE-free.
class SvfCascade
{
public:
    using Mode = SvfFilter::Mode;

    void setSampleRate (double sr) noexcept { s1.setSampleRate (sr); s2.setSampleRate (sr); }
    void setMode (Mode m)          noexcept { s1.setMode (m); s2.setMode (m); }
    void setSlope24 (bool on)      noexcept { if (on && ! use24) s2.reset(); use24 = on; }
    void setCutoff (float hz)      noexcept { s1.setCutoff (hz); s2.setCutoff (hz); }
    void setResonance (float r)    noexcept { s1.setResonance (r); s2.setResonance (r); }
    void setDrive (float d)        noexcept { drive = std::clamp (d, 0.0f, 1.0f); }

    void reset() noexcept { s1.reset(); s2.reset(); }

    float processSample (float x) noexcept
    {
        if (drive > 0.0f)
        {
            const float sat = std::tanh ((1.0f + 3.0f * drive) * x);
            const float w   = std::min (drive * 25.0f, 1.0f);
            x = (1.0f - w) * x + w * sat;
        }
        float y = s1.processSample (x);
        if (use24)
            y = s2.processSample (y);
        return y;
    }

private:
    SvfFilter s1, s2;
    bool  use24 = true;
    float drive = 0.0f;
};
