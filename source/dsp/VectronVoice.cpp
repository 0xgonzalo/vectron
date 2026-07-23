#include "VectronVoice.h"
#include "VectronSound.h"
#include "filter/FilterMath.h"
#include <cmath>

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
    filterStage.prepare (sampleRate);
    filtAdsr.setSampleRate (sampleRate);
    filterCutoffHz.reset (sampleRate, 0.02);
    filterReso.reset (sampleRate, 0.01);
    driveAmount.reset (sampleRate, 0.01);
    driveTrimGain.reset (sampleRate, 0.01);
    modLfo[0].setSampleRate (sampleRate);
    modLfo[1].setSampleRate (sampleRate);
    modLfo[0].setSeed (1u);
    modLfo[1].setSeed (2u);                          // same seeds in every voice: Global mode lines up
    modAdsr.setSampleRate (sampleRate);
    ccSmoothCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));   // ~5 ms
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

    filterStage.setEngine (static_cast<FilterStage::Engine> (params.filterType));
    filterStage.setMode (static_cast<FilterStage::Mode> (params.filterMode));
    filterStage.setSlope24 (params.filterSlope == 1);
    filterStage.setDrive (params.filterDrive);
    filterCutoffHz.setTargetValue (params.filterCutoff);
    filterReso.setTargetValue (params.filterReso);

    driveShaper.setType (static_cast<DriveShaper::Type> (params.driveType));
    driveAmount.setTargetValue (params.driveAmount);
    driveTrimGain.setTargetValue (juce::Decibels::decibelsToGain (params.driveTrimDb));

    for (int n = 0; n < 2; ++n)
    {
        modLfo[n].setShape (static_cast<ModLfo::Shape> (params.modLfoShape[n]));
        modLfo[n].setPolarity (static_cast<ModLfo::Polarity> (params.modLfoPolarity[n]));
        modLfo[n].setPhaseOffsetDegrees (params.modLfoPhaseDeg[n]);
        modLfo[n].setFadeInSeconds (params.modLfoFadeIn[n]);
        modLfo[n].setRate (params.modLfoRateHz[n]);
        appliedRateMod[n] = 0.0f;                    // base rate just (re)applied
    }
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
    // Note: noiseGen is intentionally NOT reset here — noise has no phase to click on,
    // so carrying pink/brown/tuned-BP/S&H state across a voice-steal gives continuous
    // texture rather than a reset transient. (engine + subOsc DO reset phase.)
    noiseGen.setNoteFrequency ((float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, masterTuneHz));
    lfo[0].reset();
    lfo[1].reset();
    baseX.setCurrentAndTargetValue (params.baseX);
    baseY.setCurrentAndTargetValue (params.baseY);
    vectorLevel.setCurrentAndTargetValue (params.vectorLevel);
    subLevel.setCurrentAndTargetValue (params.subLevel);
    currentNote = midiNoteNumber;
    filterStage.reset();
    filterCutoffHz.setCurrentAndTargetValue (params.filterCutoff);
    filterReso.setCurrentAndTargetValue (params.filterReso);
    driveAmount.setCurrentAndTargetValue (params.driveAmount);
    driveTrimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.driveTrimDb));
    filtAdsr.reset();
    filtAdsr.noteOn();
    modAdsr.reset();
    modAdsr.noteOn();
    noteRng = noteRng * 1664525u + 1013904223u;
    randNote = (float) ((noteRng >> 8) & 0xFFFFFFu) / 16777215.0f * 2.0f - 1.0f;
    for (int n = 0; n < 2; ++n)
    {
        if (params.modLfoGlobal[n])
        {
            modLfo[n].setSeed ((uint32_t) n + 1u);             // fixed: voices line up
            modLfo[n].startFadeIn();                           // phase stays global
        }
        else
        {
            // Per-note seed so S&H / Random shapes don't replay the same
            // sequence every note (fixed seed is only needed in Global mode).
            modLfo[n].setSeed (((uint32_t) n + 1u) ^ (noteRng * 2654435761u));
            modLfo[n].retrigger();
        }
    }
    prevLfoRateMod[0] = prevLfoRateMod[1] = 0.0f;
    level = velocity;
    ampAdsr.noteOn();
}

void VectronVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        ampAdsr.noteOff();
        filtAdsr.noteOff();
        modAdsr.noteOff();
    }
    else
    {
        ampAdsr.reset();
        filtAdsr.reset();
        modAdsr.reset();
        clearCurrentNote();
    }
}

void VectronVoice::renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    if (! ampAdsr.isActive())
        return;

    using MM = vectron::ModMatrix;

    // Global-mode LFOs follow the shared master phase; the startSample term keeps
    // chunked renders (MIDI-split blocks) coherent.
    for (int n = 0; n < 2; ++n)
        if (params.modLfoGlobal[n])
            modLfo[n].setAbsolutePhase (params.modLfoMasterPhase[n]
                                        + (double) startSample * modLfo[n].getPhaseIncrement());

    const float keytrackSrc = juce::jlimit (-1.0f, 1.0f, (float) (currentNote - 60) / 60.0f);
    const bool  stereo      = output.getNumChannels() >= 2;

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. envelopes (advanced exactly once per sample)
        const float ampEnv  = ampAdsr.getNextSample();
        const float filtEnv = filtAdsr.getNextSample()
                            * (1.0f - params.filtVelAmt + params.filtVelAmt * level);
        const float modEnv  = modAdsr.getNextSample()
                            * (1.0f - params.modVelAmt + params.modVelAmt * level);

        // 2. LFOs — rate cross-mod uses the previous sample's matrix output.
        // Global-mode LFOs skip it: their phase is pinned to the master
        // accumulator (base rate), so modding the local rate would just be
        // snapped back at the next block boundary (spec decision 17).
        for (int n = 0; n < 2; ++n)
        {
            const float rm = params.modLfoGlobal[n] ? 0.0f : prevLfoRateMod[n];
            if (rm != appliedRateMod[n])               // skip the exp2/divide when unrouted
            {
                modLfo[n].setRate (params.modLfoRateHz[n] * std::exp2 (rm * 3.0f));
                appliedRateMod[n] = rm;
            }
        }
        const float l1 = modLfo[0].processSample();
        const float l2 = modLfo[1].processSample();

        // smoothed MIDI CCs (kill zipper)
        modWheelSm   += ccSmoothCoef * (modWheel   - modWheelSm);
        aftertouchSm += ccSmoothCoef * (aftertouch - aftertouchSm);

        // 3. evaluate the matrix
        float sources[MM::kNumSources];
        sources[MM::SrcLfo1]       = l1;
        sources[MM::SrcLfo2]       = l2;
        sources[MM::SrcAmpEnv]     = ampEnv;
        sources[MM::SrcFilterEnv]  = filtEnv;
        sources[MM::SrcModEnv]     = modEnv;
        sources[MM::SrcVelocity]   = level;
        sources[MM::SrcModWheel]   = modWheelSm;
        sources[MM::SrcAftertouch] = aftertouchSm;
        sources[MM::SrcKeyTrack]   = keytrackSrc;
        sources[MM::SrcNoiseSH]    = noiseGen.getSampleHold();   // previous sample's S&H
        sources[MM::SrcRandomNote] = randNote;

        float dest[MM::kNumDests];
        MM::evaluate (params.slots, sources, dest);
        prevLfoRateMod[0] = juce::jlimit (-1.0f, 1.0f, dest[MM::DstLfo1Rate]);
        prevLfoRateMod[1] = juce::jlimit (-1.0f, 1.0f, dest[MM::DstLfo2Rate]);

        // 4. apply oscillator mods (full scale: pitch +/-12 st, PW +/-0.45, level +/-1)
        for (int o = 0; o < 4; ++o)
        {
            engine.setPitchModSemis (o, 12.0f * dest[MM::DstOscAPitch + o]);
            engine.setPulseWidth (o, juce::jlimit (0.05f, 0.95f,
                                     params.oscPw[o] + 0.45f * dest[MM::DstOscAPw + o]));
            engine.setLevel (o, juce::jlimit (0.0f, 1.0f,
                                params.oscLevel[o] + dest[MM::DstOscALevel + o]));
        }

        const float lx = lfo[0].processSample();                 // Phase 2 axis LFOs
        const float ly = lfo[1].processSample();
        const float fx = juce::jlimit (-1.0f, 1.0f, baseX.getNextValue() + lx + dest[MM::DstVectorX]);
        const float fy = juce::jlimit (-1.0f, 1.0f, baseY.getNextValue() + ly + dest[MM::DstVectorY]);
        engine.setVectorPosition (fx, fy);

        // 5. noise mods (color/cutoff/level; NoiseGenerator smooths internally)
        noiseGen.setColor (juce::jlimit (0.0f, 1.0f, params.noiseColor + dest[MM::DstNoiseColor]));
        noiseGen.setNoiseFilter (static_cast<NoiseGenerator::FilterType> (params.noiseFilterType),
                                 juce::jlimit (20.0f, 20000.0f,
                                     params.noiseCutoff * std::exp2 (5.0f * dest[MM::DstNoiseCutoff])),
                                 params.noiseReso);
        noiseGen.setLevel (juce::jlimit (0.0f, 1.0f, params.noiseLevel + dest[MM::DstNoiseLevel]));

        const float vec   = engine.processSample() * vectorLevel.getNextValue();
        const float sub   = subOsc.processSample()
                          * juce::jlimit (0.0f, 1.0f, subLevel.getNextValue() + dest[MM::DstSubLevel]);
        const float noise = noiseGen.processSample();

        // 6. filter + drive (cutoff: keytrack + env + matrix octaves)
        filterStage.setResonance (juce::jlimit (0.0f, 1.0f,
                                    filterReso.getNextValue() + dest[MM::DstFilterReso]));
        filterStage.setCutoff (vectron::effectiveCutoffHz (filterCutoffHz.getNextValue(),
                                                           currentNote,
                                                           params.filterKeytrack,
                                                           filtEnv,
                                                           params.filterEnvAmount,
                                                           5.0f * dest[MM::DstFilterCutoff]));
        driveShaper.setAmount (juce::jlimit (0.0f, 1.0f,
                                 driveAmount.getNextValue() + dest[MM::DstDriveAmount]));
        driveShaper.setTrimGain (driveTrimGain.getNextValue());

        float s = vec + sub + noise;
        if (params.drivePosition == 0) s = driveShaper.processSample (s);
        s = filterStage.processSample (s);
        if (params.drivePosition == 1) s = driveShaper.processSample (s);

        // 7. VCA: env x velocity sensitivity x matrix amp mod
        const float velGain = 1.0f - params.ampVelSens + params.ampVelSens * level;
        const float ampMod  = juce::jlimit (0.0f, 2.0f, 1.0f + dest[MM::DstAmpLevel]);
        s *= ampEnv * velGain * ampMod * 0.3f;

        // 8. equal-power pan (center-normalized: unmodulated == Phase 4 output)
        if (stereo)
        {
            const float pan   = juce::jlimit (-1.0f, 1.0f, dest[MM::DstPan]);
            const float theta = juce::MathConstants<float>::pi * 0.25f * (pan + 1.0f);
            const float gainL = juce::MathConstants<float>::sqrt2 * std::cos (theta);
            const float gainR = juce::MathConstants<float>::sqrt2 * std::sin (theta);
            output.addSample (0, startSample + i, s * gainL);
            output.addSample (1, startSample + i, s * gainR);
            for (int ch = 2; ch < output.getNumChannels(); ++ch)
                output.addSample (ch, startSample + i, s);
        }
        else
        {
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample (ch, startSample + i, s);
        }

        if (! ampAdsr.isActive())
        {
            clearCurrentNote();
            return;
        }
    }
}
