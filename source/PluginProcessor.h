#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/VectronVoice.h"
#include "dsp/osc/VectorTrajectory.h"

class VectronProcessor : public juce::AudioProcessor, private juce::ValueTree::Listener
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

    // Phase 5: mod LFOs [lfo1=0, lfo2=1]
    std::atomic<float>* pModLfoShape[2]    { nullptr, nullptr };
    std::atomic<float>* pModLfoRate[2]     { nullptr, nullptr };
    std::atomic<float>* pModLfoSync[2]     { nullptr, nullptr };
    std::atomic<float>* pModLfoSyncDiv[2]  { nullptr, nullptr };
    std::atomic<float>* pModLfoPhase[2]    { nullptr, nullptr };
    std::atomic<float>* pModLfoFadeIn[2]   { nullptr, nullptr };
    std::atomic<float>* pModLfoPolarity[2] { nullptr, nullptr };
    std::atomic<float>* pModLfoMode[2]     { nullptr, nullptr };

    // Phase 5: mod env + amp velocity
    std::atomic<float>* pModAttack  { nullptr };
    std::atomic<float>* pModDecay   { nullptr };
    std::atomic<float>* pModSustain { nullptr };
    std::atomic<float>* pModRelease { nullptr };
    std::atomic<float>* pModVelAmt  { nullptr };
    std::atomic<float>* pAmpVelSens { nullptr };

    // Phase 5: mod matrix slots
    std::atomic<float>* pModSrc[8] {};
    std::atomic<float>* pModDst[8] {};
    std::atomic<float>* pModAmt[8] {};
    std::atomic<float>* pModEn[8]  {};

    // Global-mode LFO master phase accumulators (cycles, unwrapped; audio thread only)
    double masterLfoPhase[2] { 0.0, 0.0 };

    // Phase 6: trajectory params
    std::atomic<float>* pTrajMode      { nullptr };
    std::atomic<float>* pTrajDepth     { nullptr };
    std::atomic<float>* pTrajRate      { nullptr };
    std::atomic<float>* pTrajSync      { nullptr };
    std::atomic<float>* pTrajLoopStart { nullptr };
    std::atomic<float>* pTrajLoopEnd   { nullptr };
    std::atomic<float>* pTrajLoopDir   { nullptr };
    std::atomic<float>* pTrajInterp    { nullptr };
    std::atomic<float>* pTrajTrigger   { nullptr };
    std::atomic<float>* pTrajRetrigger { nullptr };
    std::atomic<float>* pTrajRecPoints { nullptr };

    // Phase 6: trajectory model handoff (spec decision 3). Message thread rewrites
    // sharedTrajModel under trajLock and bumps trajVersion; the audio thread TRY-locks
    // on version change and copies into audioTrajModel, keeping last-good on failure.
    vectron::TrajectoryModel sharedTrajModel;
    juce::SpinLock trajLock;
    std::atomic<int> trajVersion { 1 };
    vectron::TrajectoryModel audioTrajModel;
    int  audioTrajVersion  = 0;
    vectron::TrajectoryPlayhead masterTraj;      // free-running, always Loop semantics
    bool masterTrajStarted = false;

    void ensureTrajectoryState();
    void refreshTrajectoryModel();
    void maybeRefreshTrajectory (const juce::ValueTree& changed);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override {}
    void valueTreeRedirected (juce::ValueTree&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VectronProcessor)
};
