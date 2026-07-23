#pragma once
#include <juce_dsp/juce_dsp.h>
#include "SvfCascade.h"

// Per-voice main filter (PRD §5.6): switchable SVF (clean, 12/24 dB, incl.
// Notch) or juce::dsp::LadderFilter (Moog-style). Ladder has no Notch — falls
// back to Ladder BP at the selected slope (locked design decision).
class FilterStage
{
public:
    enum class Engine { SVF, Ladder };
    enum class Mode   { LP, BP, HP, Notch };   // order == filter_mode param == SvfFilter::Mode

    void prepare (double sampleRate);
    void setEngine (Engine e) noexcept;        // resets state when the engine changes
    void setMode (Mode m) noexcept;
    void setSlope24 (bool on) noexcept;
    void setCutoff (float hz) noexcept;        // already-modulated value, called per sample
    void setResonance (float r) noexcept;
    void setDrive (float d) noexcept;          // 0..1; control-rate only
    void reset() noexcept;
    float processSample (float x) noexcept;

private:
    // LadderFilter's per-sample API is protected; expose it for single-sample use.
    struct PerSampleLadder : juce::dsp::LadderFilter<float>
    {
        using juce::dsp::LadderFilter<float>::processSample;
        using juce::dsp::LadderFilter<float>::updateSmoothers;
    };

    void updateLadderMode() noexcept;

    SvfCascade      svf;
    PerSampleLadder ladder;
    Engine engine  = Engine::SVF;
    Mode   mode    = Mode::LP;
    bool   slope24 = true;
};
