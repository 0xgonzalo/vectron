#pragma once
#include <cmath>
#include <cstdint>

class VectorLfo
{
public:
    enum class Shape { Sine, Triangle, Saw, Square, SampleHold };

    void setSampleRate (double sr) noexcept { sampleRate = sr; updateIncrement(); }
    void setRate (float hz)        noexcept { rate = hz; updateIncrement(); }
    void setDepth (float d)        noexcept { depth = d; }
    void setShape (Shape s)        noexcept { shape = s; }

    void reset() noexcept
    {
        phase = 0.0f;
        shValue = nextRandom();
    }

    float processSample() noexcept
    {
        float v = 0.0f;
        switch (shape)
        {
            case Shape::Sine:       v = std::sin (kTwoPi * phase); break;
            case Shape::Triangle:   v = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase); break;
            case Shape::Saw:        v = 2.0f * phase - 1.0f; break;
            case Shape::Square:     v = (phase < 0.5f) ? 1.0f : -1.0f; break;
            case Shape::SampleHold: v = shValue; break;
        }

        phase += increment;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            shValue = nextRandom();   // resample S&H on each wrap
        }
        return v * depth;
    }

private:
    static constexpr float kTwoPi = 6.283185307179586f;

    void updateIncrement() noexcept
    {
        increment = (sampleRate > 0.0) ? static_cast<float> (rate / sampleRate) : 0.0f;
    }

    float nextRandom() noexcept
    {
        rngState = rngState * 1664525u + 1013904223u;            // LCG
        const float unit = static_cast<float> ((rngState >> 8) & 0xFFFFFFu) / 16777215.0f; // [0,1]
        return unit * 2.0f - 1.0f;                               // [-1,1]
    }

    double  sampleRate = 44100.0;
    float   rate       = 1.0f;
    float   depth      = 0.0f;
    float   increment  = 0.0f;
    float   phase      = 0.0f;
    float   shValue    = 0.0f;
    Shape   shape      = Shape::Sine;
    uint32_t rngState  = 22222u;
};
