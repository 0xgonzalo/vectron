#include "VectronVoice.h"
#include "VectronSound.h"

void VectronVoice::prepare (double sampleRate, int /*blockSize*/)
{
    engine.setSampleRate (sampleRate);
    lfo[0].setSampleRate (sampleRate);
    lfo[1].setSampleRate (sampleRate);
    ampAdsr.setSampleRate (sampleRate);
    baseX.reset (sampleRate, 0.01);
    baseY.reset (sampleRate, 0.01);
    vectorLevel.reset (sampleRate, 0.01);
    subOsc.setSampleRate (sampleRate);
    noiseGen.setSampleRate (sampleRate);
    subLevel.reset (sampleRate, 0.01);
    applyParams();
}

void VectronVoice::applyParams() noexcept
{
    for (int i = 0; i < 4; ++i)
    {
        engine.setWave (i, static_cast<PolyBlepOscillator::Wave> (params.oscWave[i]));
        engine.setDetune (i, params.oscOct[i], params.oscCoarse[i], params.oscFine[i]);
        engine.setPulseWidth (i, params.oscPw[i]);
        engine.setLevel (i, params.oscLevel[i]);
        engine.setPhaseResetEnabled (i, params.oscPhaseReset[i]);
    }
    engine.setXfadeMode (static_cast<VectorEngine::Xfade> (params.xfade));

    for (int a = 0; a < 2; ++a)
    {
        lfo[a].setRate (params.lfoRate[a]);
        lfo[a].setDepth (params.lfoDepth[a]);
        lfo[a].setShape (static_cast<VectorLfo::Shape> (params.lfoShape[a]));
    }

    baseX.setTargetValue (params.baseX);
    baseY.setTargetValue (params.baseY);
    vectorLevel.setTargetValue (params.vectorLevel);

    subOsc.setWave (static_cast<SubOscillator::Wave> (params.subWave));
    subOsc.setOctave (params.subOct == 0 ? -1 : -2);
    subLevel.setTargetValue (params.subLevel);

    noiseGen.setColor (params.noiseColor);
    noiseGen.setTuned (params.noiseTuned);
    noiseGen.setTunedPitch (params.noisePitch);
    noiseGen.setKeytrack (params.noiseKeytrack);
    noiseGen.setNoiseFilter (static_cast<NoiseGenerator::FilterType> (params.noiseFilterType),
                             params.noiseCutoff, params.noiseReso);
    noiseGen.setLevel (params.noiseLevel);
    noiseGen.setShRate (params.noiseShRate);
    noiseGen.setShGlide (params.noiseShGlide);
}

bool VectronVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<VectronSound*> (sound) != nullptr;
}

void VectronVoice::startNote (int midiNoteNumber, float velocity,
                              juce::SynthesiserSound*, int)
{
    engine.setNoteFrequency ((float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, masterTuneHz));
    engine.noteOn();
    subOsc.setNoteFrequency ((float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, masterTuneHz));
    subOsc.noteOn();
    noiseGen.setNoteFrequency ((float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, masterTuneHz));
    lfo[0].reset();
    lfo[1].reset();
    baseX.setCurrentAndTargetValue (params.baseX);
    baseY.setCurrentAndTargetValue (params.baseY);
    vectorLevel.setCurrentAndTargetValue (params.vectorLevel);
    subLevel.setCurrentAndTargetValue (params.subLevel);
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
        const float lx = lfo[0].processSample();
        const float ly = lfo[1].processSample();
        const float fx = juce::jlimit (-1.0f, 1.0f, baseX.getNextValue() + lx);
        const float fy = juce::jlimit (-1.0f, 1.0f, baseY.getNextValue() + ly);
        engine.setVectorPosition (fx, fy);

        const float vec   = engine.processSample() * vectorLevel.getNextValue();
        const float sub   = subOsc.processSample() * subLevel.getNextValue();
        const float noise = noiseGen.processSample();

        const float env = ampAdsr.getNextSample();
        const float s   = (vec + sub + noise) * env * level * 0.3f;

        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            output.addSample (ch, startSample + i, s);

        if (! ampAdsr.isActive())
        {
            clearCurrentNote();
            return;
        }
    }
}
