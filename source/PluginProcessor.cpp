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
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<VectronVoice*> (synth.getVoice (i)))
            v->setAmpAdsr (ampParams);

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    const float volDb = apvts.getRawParameterValue ("master_volume")->load();
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (volDb, -60.0f));
    buffer.applyGainRamp (0, buffer.getNumSamples(),
                          masterGain.getCurrentValue(),
                          masterGain.skip (buffer.getNumSamples()));
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
