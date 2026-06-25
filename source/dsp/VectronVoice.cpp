#include "VectronVoice.h"
#include "VectronSound.h"

void VectronVoice::prepare (double sampleRate, int /*blockSize*/)
{
    osc.setSampleRate (sampleRate);
    osc.setWave (PolyBlepOscillator::Wave::Saw);
    ampAdsr.setSampleRate (sampleRate);
}

bool VectronVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<VectronSound*> (sound) != nullptr;
}

void VectronVoice::startNote (int midiNoteNumber, float velocity,
                              juce::SynthesiserSound*, int)
{
    osc.setFrequency ((float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, masterTuneHz));
    osc.reset (0.0f);
    level = velocity;
    ampAdsr.noteOn();
}

void VectronVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        ampAdsr.noteOff();
    }
    else
    {
        ampAdsr.reset();
        clearCurrentNote();
    }
}

void VectronVoice::renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    if (! ampAdsr.isActive())
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const float env = ampAdsr.getNextSample();
        const float s   = osc.processSample() * env * level * 0.3f; // headroom trim

        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            output.addSample (ch, startSample + i, s);

        if (! ampAdsr.isActive())
        {
            clearCurrentNote();
            return;
        }
    }
}
