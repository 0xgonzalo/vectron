#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "osc/VectorEngine.h"
#include "osc/VectorLfo.h"
#include "osc/SubOscillator.h"
#include "noise/NoiseGenerator.h"
#include "filter/FilterStage.h"
#include "drive/DriveShaper.h"

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

    // Sub oscillator
    int   subWave  { 0 };      // 0 Sine, 1 Triangle, 2 Square
    int   subOct   { 0 };      // 0 -> -1 oct, 1 -> -2 oct
    float subLevel { 0.0f };

    // Noise generator
    float noiseColor      { 0.0f };
    bool  noiseTuned      { false };
    float noisePitch      { 0.0f };
    float noiseKeytrack   { 100.0f };
    int   noiseFilterType { 2 };     // 0 HP, 1 BP, 2 LP
    float noiseCutoff     { 20000.0f };
    float noiseReso       { 0.0f };
    float noiseLevel      { 0.0f };
    float noiseShRate     { 5.0f };
    float noiseShGlide    { 0.0f };

    // Filter + drive (Phase 4)
    int   filterType      { 0 };       // 0 SVF, 1 Ladder
    int   filterMode      { 0 };       // 0 LP, 1 BP, 2 HP, 3 Notch
    int   filterSlope     { 1 };       // 0 -> 12 dB, 1 -> 24 dB
    float filterCutoff    { 1000.0f };
    float filterReso      { 0.0f };
    float filterDrive     { 0.0f };
    float filterKeytrack  { 0.0f };    // -100 .. +100 %
    float filterEnvAmount { 0.0f };    // -1 .. +1
    int   driveType       { 0 };       // 0 Tanh, 1 Hard, 2 Foldback
    float driveAmount     { 0.0f };
    float driveTrimDb     { 0.0f };
    int   drivePosition   { 0 };       // 0 Pre-filter, 1 Post-filter
    float filtVelAmt      { 0.0f };
};

class VectronVoice : public juce::SynthesiserVoice
{
public:
    void prepare (double sampleRate, int blockSize);
    void setAmpAdsr (const juce::ADSR::Parameters& p) { ampAdsr.setParameters (p); }
    void setFiltAdsr (const juce::ADSR::Parameters& p) { filtAdsr.setParameters (p); }
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
    juce::SmoothedValue<float> vectorLevel;
    SubOscillator subOsc;
    NoiseGenerator noiseGen;
    juce::SmoothedValue<float> subLevel;
    FilterStage filterStage;
    DriveShaper driveShaper;
    juce::ADSR  filtAdsr;
    juce::SmoothedValue<float> filterCutoffHz, filterReso, driveAmount, driveTrimGain;
    int currentNote = 60;
    juce::ADSR   ampAdsr;
    VectronVoiceParams params;
    float level = 0.0f;
    float masterTuneHz = 440.0f;
};
