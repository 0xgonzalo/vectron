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

    // Push current ADSR params into voices (control rate, per block).
    const juce::ADSR::Parameters ampParams {
        apvts.getRawParameterValue ("amp_attack")->load(),
        apvts.getRawParameterValue ("amp_decay")->load(),
        apvts.getRawParameterValue ("amp_sustain")->load(),
        apvts.getRawParameterValue ("amp_release")->load() };
    const float tuneHz = apvts.getRawParameterValue ("master_tune")->load();

    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    VectronVoiceParams vp;
    const char* oscIds[4] { "oscA", "oscB", "oscC", "oscD" };
    for (int i = 0; i < 4; ++i)
    {
        const juce::String id { oscIds[i] };
        vp.oscWave[i]       = (int)   raw ((id + "_wave").toRawUTF8());
        vp.oscOct[i]        = (int)   raw ((id + "_oct").toRawUTF8());
        vp.oscCoarse[i]     = (int)   raw ((id + "_coarse").toRawUTF8());
        vp.oscFine[i]       =         raw ((id + "_fine").toRawUTF8());
        vp.oscPw[i]         =         raw ((id + "_pw").toRawUTF8());
        vp.oscLevel[i]      =         raw ((id + "_level").toRawUTF8());
        vp.oscPhaseReset[i] =         raw ((id + "_phaseReset").toRawUTF8()) > 0.5f;
    }
    vp.xfade       = (int) raw ("vector_xfade");
    vp.vectorLevel =       raw ("vector_level");
    vp.baseX       =       raw ("vector_x");
    vp.baseY       =       raw ("vector_y");
    const char* axisId[2] { "vector_x", "vector_y" };
    for (int a = 0; a < 2; ++a)
    {
        const juce::String id { axisId[a] };
        vp.lfoRate[a]  =       raw ((id + "LfoRate").toRawUTF8());
        vp.lfoDepth[a] =       raw ((id + "LfoDepth").toRawUTF8());
        vp.lfoShape[a] = (int) raw ((id + "LfoShape").toRawUTF8());
    }

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<VectronVoice*> (synth.getVoice (i)))
        {
            v->setAmpAdsr (ampParams);
            v->setMasterTune (tuneHz);
            v->setVectorParams (vp);
        }

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    const float volDb = apvts.getRawParameterValue ("master_volume")->load();
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (volDb, -60.0f));
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
