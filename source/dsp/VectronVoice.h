#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "osc/VectorEngine.h"
#include "osc/VectorLfo.h"

struct VectronVoiceParams
{
    int   oscWave[4]       { 2, 3, 0, 1 };
    int   oscOct[4]        { 0, 0, 0, 0 };
    int   oscCoarse[4]     { 0, 0, 0, 0 };
    float oscFine[4]       { 0.0f, 0.0f, 0.0f, 0.0f };
    float oscPw[4]         { 0.5f, 0.5f, 0.5f, 0.5f };
    float oscLevel[4]      { 1.0f, 1.0f, 1.0f, 1.0f };
    bool  oscPhaseReset[4] { true, true, true, true };
    int   xfade            { 0 };           // 0 Linear, 1 Equal-Power
    float vectorLevel      { 1.0f };
    float baseX            { 0.0f };
    float baseY            { 0.0f };
    float lfoRate[2]       { 1.0f, 1.0f };  // [0]=X, [1]=Y
    float lfoDepth[2]      { 0.0f, 0.0f };
    int   lfoShape[2]      { 0, 0 };
};

class VectronVoice : public juce::SynthesiserVoice
{
public:
    void prepare (double sampleRate, int blockSize);
    void setAmpAdsr (const juce::ADSR::Parameters& p) { ampAdsr.setParameters (p); }
    void setMasterTune (float a4Hz) { masterTuneHz = a4Hz; }
    void setVectorParams (const VectronVoiceParams& p) noexcept { params = p; applyParams(); }

    bool canPlaySound (juce::SynthesiserSound*) override;
    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override;

private:
    void applyParams() noexcept;

    VectorEngine engine;
    VectorLfo    lfo[2];                 // [0]=X, [1]=Y
    juce::SmoothedValue<float> baseX, baseY;
    juce::ADSR   ampAdsr;
    VectronVoiceParams params;
    float level = 0.0f;
    float masterTuneHz = 440.0f;
};
