#pragma once
#include <cmath>
#include <algorithm>

class PolyBlepOscillator
{
public:
    enum class Wave { Sine, Triangle, Saw, Pulse };

    void setSampleRate (double sr) noexcept { sampleRate = sr; updateIncrement(); }
    void setFrequency (float hz)   noexcept { frequency  = hz; updateIncrement(); }
    void setWave (Wave w)          noexcept { wave = w; }
    void setPulseWidth (float pw) noexcept { pulseWidth = std::clamp (pw, 0.05f, 0.95f); }
    void reset (float startPhase = 0.0f) noexcept { phase = startPhase; }

    float processSample() noexcept
    {
        const float t = phase;
        float value = 0.0f;

        switch (wave)
        {
            case Wave::Sine:
                value = std::sin (kTwoPi * t);
                break;
            case Wave::Triangle:
                // TODO: implement triangle wave
                value = 0.0f;
                break;
            case Wave::Saw:
                value  = 2.0f * t - 1.0f;          // naive saw [-1,1]
                value -= polyBlep (t, increment);  // correct the wrap discontinuity
                break;
            case Wave::Pulse:
            {
                value  = (t < pulseWidth) ? 1.0f : -1.0f;
                value += polyBlep (t, increment);            // rising edge at phase 0 (+2 step)
                float t2 = t - pulseWidth;                   // phase relative to the falling edge
                if (t2 < 0.0f) t2 += 1.0f;
                value -= polyBlep (t2, increment);           // falling edge at phase = pw (-2 step)
                break;
            }
        }

        phase += increment;
        if (phase >= 1.0f) phase -= 1.0f;
        return value;
    }

private:
    static constexpr float kTwoPi = 6.283185307179586f;

    void updateIncrement() noexcept
    {
        increment = (sampleRate > 0.0) ? static_cast<float> (frequency / sampleRate) : 0.0f;
    }

    static float polyBlep (float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt)            { t /= dt;              return (t + t) - (t * t) - 1.0f; }
        if (t > 1.0f - dt)     { t = (t - 1.0f) / dt;  return (t * t) + (t + t) + 1.0f; }
        return 0.0f;
    }

    double sampleRate = 44100.0;
    float  frequency  = 440.0f;
    float  increment  = 0.0f;
    float  phase      = 0.0f;
    float  pulseWidth = 0.5f;
    Wave   wave       = Wave::Saw;
};
