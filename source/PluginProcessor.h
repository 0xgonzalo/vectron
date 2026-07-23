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

    // Cached APVTS parameter pointers — resolved once in constructor, read lock-free on audio thread.
    std::atomic<float>* pAmpAttack    { nullptr };
    std::atomic<float>* pAmpDecay     { nullptr };
    std::atomic<float>* pAmpSustain   { nullptr };
    std::atomic<float>* pAmpRelease   { nullptr };
    std::atomic<float>* pMasterVolume { nullptr };
    std::atomic<float>* pMasterTune   { nullptr };

    // Per-oscillator [A=0, B=1, C=2, D=3] — mirrors VectronVoiceParams layout
    std::atomic<float>* pOscWave[4]       { nullptr, nullptr, nullptr, nullptr };
    std::atomic<float>* pOscOct[4]        { nullptr, nullptr, nullptr, nullptr };
    std::atomic<float>* pOscCoarse[4]     { nullptr, nullptr, nullptr, nullptr };
    std::atomic<float>* pOscFine[4]       { nullptr, nullptr, nullptr, nullptr };
    std::atomic<float>* pOscPw[4]         { nullptr, nullptr, nullptr, nullptr };
    std::atomic<float>* pOscLevel[4]      { nullptr, nullptr, nullptr, nullptr };
    std::atomic<float>* pOscPhaseReset[4] { nullptr, nullptr, nullptr, nullptr };

    // Vector mix/position params
    std::atomic<float>* pVectorX     { nullptr };
    std::atomic<float>* pVectorY     { nullptr };
    std::atomic<float>* pVectorXfade { nullptr };
    std::atomic<float>* pVectorLevel { nullptr };

    // Per-axis LFO [x=0, y=1] — mirrors VectronVoiceParams lfoRate/lfoDepth/lfoShape
    std::atomic<float>* pLfoRate[2]  { nullptr, nullptr };
    std::atomic<float>* pLfoDepth[2] { nullptr, nullptr };
    std::atomic<float>* pLfoShape[2] { nullptr, nullptr };

    // Sub oscillator
    std::atomic<float>* pSubWave  { nullptr };
    std::atomic<float>* pSubOct   { nullptr };
    std::atomic<float>* pSubLevel { nullptr };

    // Noise generator
    std::atomic<float>* pNoiseColor      { nullptr };
    std::atomic<float>* pNoiseTuned      { nullptr };
    std::atomic<float>* pNoisePitch      { nullptr };
    std::atomic<float>* pNoiseKeytrack   { nullptr };
    std::atomic<float>* pNoiseFilterType { nullptr };
    std::atomic<float>* pNoiseCutoff     { nullptr };
    std::atomic<float>* pNoiseReso       { nullptr };
    std::atomic<float>* pNoiseLevel      { nullptr };
    std::atomic<float>* pNoiseShRate     { nullptr };
    std::atomic<float>* pNoiseShGlide    { nullptr };

    // Filter + drive + filter env
    std::atomic<float>* pFilterType      { nullptr };
    std::atomic<float>* pFilterMode      { nullptr };
    std::atomic<float>* pFilterSlope     { nullptr };
    std::atomic<float>* pFilterCutoff    { nullptr };
    std::atomic<float>* pFilterReso      { nullptr };
    std::atomic<float>* pFilterDrive     { nullptr };
    std::atomic<float>* pFilterKeytrack  { nullptr };
    std::atomic<float>* pFilterEnvAmount { nullptr };
    std::atomic<float>* pDriveType       { nullptr };
    std::atomic<float>* pDriveAmount     { nullptr };
    std::atomic<float>* pDriveTrim       { nullptr };
    std::atomic<float>* pDrivePosition   { nullptr };
    std::atomic<float>* pFiltAttack      { nullptr };
    std::atomic<float>* pFiltDecay       { nullptr };
    std::atomic<float>* pFiltSustain     { nullptr };
    std::atomic<float>* pFiltRelease     { nullptr };
    std::atomic<float>* pFiltVelAmt      { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VectronProcessor)
};
