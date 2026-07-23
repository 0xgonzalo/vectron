#pragma once
#include <cmath>

// Exponential-segment ADSR (one-pole toward overshoot targets, EarLevel/Redmon
// style). API mirrors juce::ADSR so the voice swaps it in place; also readable
// mid-block as a mod source via getCurrentValue(). JUCE-free.
class AdsrEnvelope
{
public:
    struct Parameters
    {
        float attack  = 0.005f;   // seconds
        float decay   = 0.2f;
        float sustain = 0.8f;     // level 0..1
        float release = 0.3f;
    };

    void setSampleRate (double sr) noexcept
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        recalc();
    }

    void setParameters (const Parameters& p) noexcept
    {
        params = p;
        recalc();
    }

    void reset() noexcept   { state = State::Idle; value = 0.0f; }
    void noteOn() noexcept  { state = State::Attack; }                       // from current value: no click
    void noteOff() noexcept { if (state != State::Idle) state = State::Release; }

    bool  isActive() const noexcept        { return state != State::Idle; }
    float getCurrentValue() const noexcept { return value; }

    float getNextSample() noexcept
    {
        switch (state)
        {
            case State::Attack:
                value = attackTarget + (value - attackTarget) * attackCoef;
                if (value >= 1.0f) { value = 1.0f; state = State::Decay; }
                break;
            case State::Decay:
                value = decayTarget + (value - decayTarget) * decayCoef;
                if (value <= params.sustain) { value = params.sustain; state = State::Sustain; }
                break;
            case State::Sustain:
                value = params.sustain;                                      // tracks knob movement
                break;
            case State::Release:
                value = releaseTarget + (value - releaseTarget) * releaseCoef;
                if (value <= kSilence) { value = 0.0f; state = State::Idle; }
                break;
            case State::Idle:
                value = 0.0f;
                break;
        }
        return value;
    }

private:
    enum class State { Idle, Attack, Decay, Sustain, Release };

    static constexpr float kSilence     = 1.0e-4f;
    static constexpr float kAttackRatio = 0.3f;     // overshoot above 1.0 -> convex attack
    static constexpr float kDecayRatio  = 1.0e-4f;  // undershoot below target -> fast-settling tail

    // One-pole coefficient that traverses the segment in ~timeSec seconds.
    float coefFor (float timeSec, float ratio) const noexcept
    {
        const float samples = timeSec * (float) sampleRate;
        if (samples < 1.0f) return 0.0f;                                     // instant
        return std::exp (-std::log ((1.0f + ratio) / ratio) / samples);
    }

    void recalc() noexcept
    {
        attackCoef    = coefFor (params.attack,  kAttackRatio);
        decayCoef     = coefFor (params.decay,   kDecayRatio);
        releaseCoef   = coefFor (params.release, kDecayRatio);
        attackTarget  = 1.0f + kAttackRatio;
        decayTarget   = params.sustain - kDecayRatio;
        releaseTarget = -kDecayRatio;
    }

    Parameters params;
    double sampleRate = 44100.0;
    float  value = 0.0f;
    float  attackCoef = 0.0f, decayCoef = 0.0f, releaseCoef = 0.0f;
    float  attackTarget = 1.3f, decayTarget = 0.0f, releaseTarget = 0.0f;
    State  state = State::Idle;
};
