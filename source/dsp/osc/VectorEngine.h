#pragma once
#include "PolyBlepOscillator.h"

class VectorEngine
{
public:
    enum class Xfade { Linear, EqualPower };
    struct Weights { float a, b, c, d; };

    static Weights computeWeights (float x, float y, Xfade mode) noexcept;

    void setSampleRate (double sr) noexcept;
    void setWave (int idx, PolyBlepOscillator::Wave w) noexcept;
    void setLevel (int idx, float level) noexcept;
    void setPulseWidth (int idx, float pw) noexcept;
    void setDetune (int idx, int octave, int coarseSemis, float fineCents) noexcept;
    void setPhaseResetEnabled (int idx, bool enabled) noexcept;
    void setXfadeMode (Xfade mode) noexcept;
    void setNoteFrequency (float baseHz) noexcept;
    void noteOn() noexcept;
    void setVectorPosition (float x, float y) noexcept;
    float processSample() noexcept;

private:
    void updateFrequency (int idx) noexcept;

    static constexpr int kNumOsc = 4;
    PolyBlepOscillator osc[kNumOsc];
    float level[kNumOsc]      { 1.0f, 1.0f, 1.0f, 1.0f };
    int   octave[kNumOsc]     { 0, 0, 0, 0 };
    int   coarse[kNumOsc]     { 0, 0, 0, 0 };
    float fine[kNumOsc]       { 0.0f, 0.0f, 0.0f, 0.0f };
    bool  phaseReset[kNumOsc] { true, true, true, true };
    float baseHz   = 440.0f;
    Xfade xfade    = Xfade::Linear;
    Weights weight { 0.25f, 0.25f, 0.25f, 0.25f };
};
