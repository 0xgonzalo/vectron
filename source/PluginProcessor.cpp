#include "PluginProcessor.h"
#include "params/ParameterLayout.h"
#include "dsp/VectronSound.h"

VectronProcessor::VectronProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", vectron::createParameterLayout())
{
    for (int i = 0; i < kNumVoices; ++i)
        synth.addVoice (new VectronVoice());
    synth.addSound (new VectronSound());

    // Resolve all parameter pointers once here — juce::String construction is fine off the audio thread.
    pAmpAttack    = apvts.getRawParameterValue ("amp_attack");
    pAmpDecay     = apvts.getRawParameterValue ("amp_decay");
    pAmpSustain   = apvts.getRawParameterValue ("amp_sustain");
    pAmpRelease   = apvts.getRawParameterValue ("amp_release");
    pMasterVolume = apvts.getRawParameterValue ("master_volume");
    pMasterTune   = apvts.getRawParameterValue ("master_tune");

    const char* oscIds[4] { "oscA", "oscB", "oscC", "oscD" };
    for (int i = 0; i < 4; ++i)
    {
        juce::String id { oscIds[i] };
        pOscWave[i]       = apvts.getRawParameterValue (id + "_wave");
        pOscOct[i]        = apvts.getRawParameterValue (id + "_oct");
        pOscCoarse[i]     = apvts.getRawParameterValue (id + "_coarse");
        pOscFine[i]       = apvts.getRawParameterValue (id + "_fine");
        pOscPw[i]         = apvts.getRawParameterValue (id + "_pw");
        pOscLevel[i]      = apvts.getRawParameterValue (id + "_level");
        pOscPhaseReset[i] = apvts.getRawParameterValue (id + "_phaseReset");
    }

    pVectorX     = apvts.getRawParameterValue ("vector_x");
    pVectorY     = apvts.getRawParameterValue ("vector_y");
    pVectorXfade = apvts.getRawParameterValue ("vector_xfade");
    pVectorLevel = apvts.getRawParameterValue ("vector_level");

    const char* axisIds[2] { "vector_x", "vector_y" };
    for (int a = 0; a < 2; ++a)
    {
        juce::String id { axisIds[a] };
        pLfoRate[a]  = apvts.getRawParameterValue (id + "LfoRate");
        pLfoDepth[a] = apvts.getRawParameterValue (id + "LfoDepth");
        pLfoShape[a] = apvts.getRawParameterValue (id + "LfoShape");
    }

    pSubWave  = apvts.getRawParameterValue ("sub_wave");
    pSubOct   = apvts.getRawParameterValue ("sub_oct");
    pSubLevel = apvts.getRawParameterValue ("sub_level");

    pNoiseColor      = apvts.getRawParameterValue ("noise_color");
    pNoiseTuned      = apvts.getRawParameterValue ("noise_tuned");
    pNoisePitch      = apvts.getRawParameterValue ("noise_pitch");
    pNoiseKeytrack   = apvts.getRawParameterValue ("noise_keytrack");
    pNoiseFilterType = apvts.getRawParameterValue ("noise_filterType");
    pNoiseCutoff     = apvts.getRawParameterValue ("noise_cutoff");
    pNoiseReso       = apvts.getRawParameterValue ("noise_reso");
    pNoiseLevel      = apvts.getRawParameterValue ("noise_level");
    pNoiseShRate     = apvts.getRawParameterValue ("noise_sh_rate");
    pNoiseShGlide    = apvts.getRawParameterValue ("noise_sh_glide");

    // Fail loudly in debug if any param ID was misspelt.
    jassert (pAmpAttack && pAmpDecay && pAmpSustain && pAmpRelease);
    jassert (pMasterVolume && pMasterTune);
    for (int i = 0; i < 4; ++i)
        jassert (pOscWave[i] && pOscOct[i] && pOscCoarse[i] && pOscFine[i]
                 && pOscPw[i] && pOscLevel[i] && pOscPhaseReset[i]);
    jassert (pVectorX && pVectorY && pVectorXfade && pVectorLevel);
    for (int a = 0; a < 2; ++a)
        jassert (pLfoRate[a] && pLfoDepth[a] && pLfoShape[a]);
    jassert (pSubWave && pSubOct && pSubLevel);
    jassert (pNoiseColor && pNoiseTuned && pNoisePitch && pNoiseKeytrack && pNoiseFilterType
             && pNoiseCutoff && pNoiseReso && pNoiseLevel && pNoiseShRate && pNoiseShGlide);
}

void VectronProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<VectronVoice*> (synth.getVoice (i)))
            v->prepare (sampleRate, samplesPerBlock);

    masterGain.reset (sampleRate, 0.02);
}

void VectronProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // All reads use pre-resolved atomic pointers — no allocations, no locks on this thread.

    // Push current ADSR params into voices (control rate, per block).
    const juce::ADSR::Parameters ampParams {
        pAmpAttack->load(),
        pAmpDecay->load(),
        pAmpSustain->load(),
        pAmpRelease->load() };
    const float tuneHz = pMasterTune->load();

    VectronVoiceParams vp;
    for (int i = 0; i < 4; ++i)
    {
        vp.oscWave[i]       = (int)  pOscWave[i]->load();
        vp.oscOct[i]        = (int)  pOscOct[i]->load();
        vp.oscCoarse[i]     = (int)  pOscCoarse[i]->load();
        vp.oscFine[i]       =        pOscFine[i]->load();
        vp.oscPw[i]         =        pOscPw[i]->load();
        vp.oscLevel[i]      =        pOscLevel[i]->load();
        vp.oscPhaseReset[i] =        pOscPhaseReset[i]->load() > 0.5f;
    }
    vp.xfade       = (int) pVectorXfade->load();
    vp.vectorLevel =       pVectorLevel->load();
    vp.baseX       =       pVectorX->load();
    vp.baseY       =       pVectorY->load();
    for (int a = 0; a < 2; ++a)
    {
        vp.lfoRate[a]  =       pLfoRate[a]->load();
        vp.lfoDepth[a] =       pLfoDepth[a]->load();
        vp.lfoShape[a] = (int) pLfoShape[a]->load();
    }

    vp.subWave  = (int) pSubWave->load();
    vp.subOct   = (int) pSubOct->load();
    vp.subLevel =       pSubLevel->load();

    vp.noiseColor      =        pNoiseColor->load();
    vp.noiseTuned      =        pNoiseTuned->load() > 0.5f;
    vp.noisePitch      =        pNoisePitch->load();
    vp.noiseKeytrack   =        pNoiseKeytrack->load();
    vp.noiseFilterType = (int)  pNoiseFilterType->load();
    vp.noiseCutoff     =        pNoiseCutoff->load();
    vp.noiseReso       =        pNoiseReso->load();
    vp.noiseLevel      =        pNoiseLevel->load();
    vp.noiseShRate     =        pNoiseShRate->load();
    vp.noiseShGlide    =        pNoiseShGlide->load();

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<VectronVoice*> (synth.getVoice (i)))
        {
            v->setAmpAdsr (ampParams);
            v->setMasterTune (tuneHz);
            v->setVectorParams (vp);
        }

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (pMasterVolume->load(), -60.0f));
    const int numSamples  = buffer.getNumSamples();
    const float startGain = masterGain.getCurrentValue();
    const float endGain   = masterGain.skip (numSamples);
    buffer.applyGainRamp (0, numSamples, startGain, endGain);
}

juce::AudioProcessorEditor* VectronProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void VectronProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void VectronProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VectronProcessor();
}
