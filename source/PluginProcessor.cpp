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

    pFilterType      = apvts.getRawParameterValue ("filter_type");
    pFilterMode      = apvts.getRawParameterValue ("filter_mode");
    pFilterSlope     = apvts.getRawParameterValue ("filter_slope");
    pFilterCutoff    = apvts.getRawParameterValue ("filter_cutoff");
    pFilterReso      = apvts.getRawParameterValue ("filter_reso");
    pFilterDrive     = apvts.getRawParameterValue ("filter_drive");
    pFilterKeytrack  = apvts.getRawParameterValue ("filter_keytrack");
    pFilterEnvAmount = apvts.getRawParameterValue ("filter_envAmount");
    pDriveType       = apvts.getRawParameterValue ("drive_type");
    pDriveAmount     = apvts.getRawParameterValue ("drive_amount");
    pDriveTrim       = apvts.getRawParameterValue ("drive_trim");
    pDrivePosition   = apvts.getRawParameterValue ("drive_position");
    pFiltAttack      = apvts.getRawParameterValue ("filt_attack");
    pFiltDecay       = apvts.getRawParameterValue ("filt_decay");
    pFiltSustain     = apvts.getRawParameterValue ("filt_sustain");
    pFiltRelease     = apvts.getRawParameterValue ("filt_release");
    pFiltVelAmt      = apvts.getRawParameterValue ("filt_velAmt");

    for (int n = 0; n < 2; ++n)
    {
        const juce::String id = "lfo" + juce::String (n + 1);
        pModLfoShape[n]    = apvts.getRawParameterValue (id + "_shape");
        pModLfoRate[n]     = apvts.getRawParameterValue (id + "_rate");
        pModLfoSync[n]     = apvts.getRawParameterValue (id + "_sync");
        pModLfoSyncDiv[n]  = apvts.getRawParameterValue (id + "_syncDiv");
        pModLfoPhase[n]    = apvts.getRawParameterValue (id + "_phase");
        pModLfoFadeIn[n]   = apvts.getRawParameterValue (id + "_fadeIn");
        pModLfoPolarity[n] = apvts.getRawParameterValue (id + "_polarity");
        pModLfoMode[n]     = apvts.getRawParameterValue (id + "_mode");
    }

    pModAttack  = apvts.getRawParameterValue ("mod_attack");
    pModDecay   = apvts.getRawParameterValue ("mod_decay");
    pModSustain = apvts.getRawParameterValue ("mod_sustain");
    pModRelease = apvts.getRawParameterValue ("mod_release");
    pModVelAmt  = apvts.getRawParameterValue ("mod_velAmt");
    pAmpVelSens = apvts.getRawParameterValue ("amp_velSens");

    for (int s = 0; s < 8; ++s)
    {
        const juce::String id = "mod" + juce::String (s + 1);
        pModSrc[s] = apvts.getRawParameterValue (id + "_src");
        pModDst[s] = apvts.getRawParameterValue (id + "_dst");
        pModAmt[s] = apvts.getRawParameterValue (id + "_amt");
        pModEn[s]  = apvts.getRawParameterValue (id + "_en");
    }

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
    jassert (pFilterType && pFilterMode && pFilterSlope && pFilterCutoff && pFilterReso
             && pFilterDrive && pFilterKeytrack && pFilterEnvAmount);
    jassert (pDriveType && pDriveAmount && pDriveTrim && pDrivePosition);
    jassert (pFiltAttack && pFiltDecay && pFiltSustain && pFiltRelease && pFiltVelAmt);
    for (int n = 0; n < 2; ++n)
        jassert (pModLfoShape[n] && pModLfoRate[n] && pModLfoSync[n] && pModLfoSyncDiv[n]
                 && pModLfoPhase[n] && pModLfoFadeIn[n] && pModLfoPolarity[n] && pModLfoMode[n]);
    jassert (pModAttack && pModDecay && pModSustain && pModRelease && pModVelAmt && pAmpVelSens);
    for (int s = 0; s < 8; ++s)
        jassert (pModSrc[s] && pModDst[s] && pModAmt[s] && pModEn[s]);
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
    const AdsrEnvelope::Parameters ampParams {
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

    vp.filterType      = (int) pFilterType->load();
    vp.filterMode      = (int) pFilterMode->load();
    vp.filterSlope     = (int) pFilterSlope->load();
    vp.filterCutoff    =       pFilterCutoff->load();
    vp.filterReso      =       pFilterReso->load();
    vp.filterDrive     =       pFilterDrive->load();
    vp.filterKeytrack  =       pFilterKeytrack->load();
    vp.filterEnvAmount =       pFilterEnvAmount->load();
    vp.driveType       = (int) pDriveType->load();
    vp.driveAmount     =       pDriveAmount->load();
    vp.driveTrimDb     =       pDriveTrim->load();
    vp.drivePosition   = (int) pDrivePosition->load();
    vp.filtVelAmt      =       pFiltVelAmt->load();

    const AdsrEnvelope::Parameters filtParams {
        pFiltAttack->load(),
        pFiltDecay->load(),
        pFiltSustain->load(),
        pFiltRelease->load() };
    const AdsrEnvelope::Parameters modParams {
        pModAttack->load(),
        pModDecay->load(),
        pModSustain->load(),
        pModRelease->load() };

    // Phase 5: tempo, mod LFOs, matrix
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm())
                bpm = *hostBpm > 0.0 ? *hostBpm : 120.0;

    // Beats per LFO cycle for each syncDiv choice (1/1 ... 1/32T), 4/4 quarter = 1 beat.
    static constexpr double divBeats[16] = { 4.0, 3.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0,
                                             0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0,
                                             0.1875, 0.125, 1.0 / 12.0 };
    const double sr = getSampleRate();
    for (int n = 0; n < 2; ++n)
    {
        const bool  sync = pModLfoSync[n]->load() > 0.5f;
        const int   div  = juce::jlimit (0, 15, (int) pModLfoSyncDiv[n]->load());
        const float hz   = sync ? (float) (bpm / (60.0 * divBeats[div]))
                                : pModLfoRate[n]->load();
        vp.modLfoShape[n]       = (int) pModLfoShape[n]->load();
        vp.modLfoRateHz[n]      = hz;
        vp.modLfoPhaseDeg[n]    = pModLfoPhase[n]->load();
        vp.modLfoFadeIn[n]      = pModLfoFadeIn[n]->load();
        vp.modLfoPolarity[n]    = (int) pModLfoPolarity[n]->load();
        vp.modLfoGlobal[n]      = pModLfoMode[n]->load() > 0.5f;
        vp.modLfoMasterPhase[n] = masterLfoPhase[n];
        if (sr > 0.0)
            masterLfoPhase[n] += (double) buffer.getNumSamples() * (double) hz / sr;
    }

    vp.modVelAmt  = pModVelAmt->load();
    vp.ampVelSens = pAmpVelSens->load();
    for (int s = 0; s < 8; ++s)
    {
        vp.slots[s].source  = (int) pModSrc[s]->load();
        vp.slots[s].dest    = (int) pModDst[s]->load();
        vp.slots[s].amount  = pModAmt[s]->load();
        vp.slots[s].enabled = pModEn[s]->load() > 0.5f;
    }

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<VectronVoice*> (synth.getVoice (i)))
        {
            v->setAmpAdsr (ampParams);
            v->setFiltAdsr (filtParams);
            v->setModAdsr (modParams);
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
