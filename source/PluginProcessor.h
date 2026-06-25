#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/VectronVoice.h"

class VectronProcessor : public juce::AudioProcessor
{
public:
    VectronProcessor();
    ~VectronProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vectron"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::Synthesiser synth;
    static constexpr int kNumVoices = 16;
    juce::SmoothedValue<float> masterGain;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VectronProcessor)
};
