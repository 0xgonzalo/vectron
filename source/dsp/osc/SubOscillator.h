#pragma once
#include <cmath>
#include "PolyBlepOscillator.h"

// Sub oscillator: a single band-limited waveform an octave (or two) below the note.
class SubOscillator
{
public:
    enum class Wave { Sine, Triangle, Square };

    void setSampleRate (double sr) noexcept { osc.setSampleRate (sr); }

    void setWave (Wave w) noexcept
    {
        switch (w)
        {
            case Wave::Sine:     osc.setWave (PolyBlepOscillator::Wave::Sine);     break;
            case Wave::Triangle: osc.setWave (PolyBlepOscillator::Wave::Triangle); break;
            case Wave::Square:   osc.setWave (PolyBlepOscillator::Wave::Pulse);
                                 osc.setPulseWidth (0.5f);                          break;
        }
    }

    void setOctave (int oct) noexcept          { octave = oct; updateFrequency(); }  // -1 or -2
    void setNoteFrequency (float hz) noexcept  { baseHz = hz;  updateFrequency(); }
    void noteOn() noexcept                     { osc.reset (0.0f); }
    float processSample() noexcept             { return osc.processSample(); }

private:
    void updateFrequency() noexcept
    {
        osc.setFrequency (baseHz * std::pow (2.0f, static_cast<float> (octave)));
    }

    PolyBlepOscillator osc;
    float baseHz = 440.0f;
    int   octave = -1;
};
