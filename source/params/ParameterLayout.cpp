#include "ParameterLayout.h"
#include "dsp/mod/ModMatrix.h"
#include <iterator>

namespace vectron
{
    using APF = juce::AudioParameterFloat;
    using API = juce::AudioParameterInt;
    using APC = juce::AudioParameterChoice;
    using APB = juce::AudioParameterBool;

    static juce::NormalisableRange<float> timeRange (float maxSeconds)
    {
        juce::NormalisableRange<float> r { 0.0f, maxSeconds, 0.0001f };
        r.setSkewForCentre (maxSeconds * 0.15f);   // log-ish: more resolution low
        return r;
    }

    static juce::NormalisableRange<float> lfoRateRange()
    {
        juce::NormalisableRange<float> r { 0.01f, 20.0f, 0.001f };
        r.setSkewForCentre (1.0f);
        return r;
    }

    static juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r { lo, hi, 0.0f };
        r.setSkewForCentre (std::sqrt (lo * hi));   // geometric centre -> log-like taper
        return r;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<APF> (juce::ParameterID { "amp_attack", 1 },
            "Amp Attack",  timeRange (10.0f), 0.005f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "amp_decay", 1 },
            "Amp Decay",   timeRange (10.0f), 0.2f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "amp_sustain", 1 },
            "Amp Sustain", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.8f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "amp_release", 1 },
            "Amp Release", timeRange (15.0f), 0.3f));

        layout.add (std::make_unique<APF> (juce::ParameterID { "master_volume", 1 },
            "Master Volume", juce::NormalisableRange<float> { -60.0f, 6.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "master_tune", 1 },
            "Master Tune", juce::NormalisableRange<float> { 415.0f, 466.0f, 0.1f }, 440.0f));

        const juce::StringArray waveChoices { "Sine", "Triangle", "Saw", "Pulse" };
        const int defaultWave[4] { 2, 3, 0, 1 };   // A=Saw, B=Pulse, C=Sine, D=Triangle
        const char* oscIds[4]   { "oscA", "oscB", "oscC", "oscD" };
        const char* oscNames[4] { "Osc A", "Osc B", "Osc C", "Osc D" };

        for (int i = 0; i < 4; ++i)
        {
            const juce::String id   { oscIds[i] };
            const juce::String name { oscNames[i] };

            layout.add (std::make_unique<APC> (juce::ParameterID { id + "_wave", 1 },
                name + " Wave", waveChoices, defaultWave[i]));
            layout.add (std::make_unique<API> (juce::ParameterID { id + "_oct", 1 },
                name + " Octave", -3, 3, 0));
            layout.add (std::make_unique<API> (juce::ParameterID { id + "_coarse", 1 },
                name + " Coarse", -24, 24, 0));
            layout.add (std::make_unique<APF> (juce::ParameterID { id + "_fine", 1 },
                name + " Fine", juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f }, 0.0f));
            layout.add (std::make_unique<APF> (juce::ParameterID { id + "_pw", 1 },
                name + " Pulse Width", juce::NormalisableRange<float> { 0.05f, 0.95f, 0.001f }, 0.5f));
            layout.add (std::make_unique<APF> (juce::ParameterID { id + "_level", 1 },
                name + " Level", juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f));
            layout.add (std::make_unique<APB> (juce::ParameterID { id + "_phaseReset", 1 },
                name + " Phase Reset", true));
        }

        layout.add (std::make_unique<APF> (juce::ParameterID { "vector_x", 1 },
            "Vector X", juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "vector_y", 1 },
            "Vector Y", juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APC> (juce::ParameterID { "vector_xfade", 1 },
            "Vector Crossfade", juce::StringArray { "Linear", "Equal-Power" }, 0));
        layout.add (std::make_unique<APF> (juce::ParameterID { "vector_level", 1 },
            "Vector Level", juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f));

        const juce::StringArray lfoShapes { "Sine", "Triangle", "Saw", "Square", "S&H" };
        const char* axisId[2]   { "vector_x", "vector_y" };
        const char* axisName[2] { "Vector X", "Vector Y" };
        for (int a = 0; a < 2; ++a)
        {
            const juce::String id   { axisId[a] };
            const juce::String name { axisName[a] };
            layout.add (std::make_unique<APF> (juce::ParameterID { id + "LfoRate", 1 },
                name + " LFO Rate", lfoRateRange(), 1.0f));
            layout.add (std::make_unique<APF> (juce::ParameterID { id + "LfoDepth", 1 },
                name + " LFO Depth", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
            layout.add (std::make_unique<APC> (juce::ParameterID { id + "LfoShape", 1 },
                name + " LFO Shape", lfoShapes, 0));
        }

        // --- Sub oscillator (PRD §5.3) ---
        layout.add (std::make_unique<APC> (juce::ParameterID { "sub_wave", 1 },
            "Sub Wave", juce::StringArray { "Sine", "Triangle", "Square" }, 0));
        layout.add (std::make_unique<APC> (juce::ParameterID { "sub_oct", 1 },
            "Sub Octave", juce::StringArray { "-1", "-2" }, 0));
        layout.add (std::make_unique<APF> (juce::ParameterID { "sub_level", 1 },
            "Sub Level", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));

        // --- Noise generator (PRD §5.4) ---
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_color", 1 },
            "Noise Color", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APB> (juce::ParameterID { "noise_tuned", 1 },
            "Noise Tuned", false));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_pitch", 1 },
            "Noise Pitch", juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_keytrack", 1 },
            "Noise Keytrack", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f));
        layout.add (std::make_unique<APC> (juce::ParameterID { "noise_filterType", 1 },
            "Noise Filter Type", juce::StringArray { "HP", "BP", "LP" }, 2));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_cutoff", 1 },
            "Noise Cutoff", logRange (20.0f, 20000.0f), 20000.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_reso", 1 },
            "Noise Resonance", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_level", 1 },
            "Noise Level", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_sh_rate", 1 },
            "Noise S&H Rate", logRange (0.1f, 50.0f), 5.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_sh_glide", 1 },
            "Noise S&H Glide", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));

        // --- Filter (PRD §5.6) ---
        layout.add (std::make_unique<APC> (juce::ParameterID { "filter_type", 1 },
            "Filter Type", juce::StringArray { "SVF", "Ladder" }, 0));
        layout.add (std::make_unique<APC> (juce::ParameterID { "filter_mode", 1 },
            "Filter Mode", juce::StringArray { "LP", "BP", "HP", "Notch" }, 0));
        layout.add (std::make_unique<APC> (juce::ParameterID { "filter_slope", 1 },
            "Filter Slope", juce::StringArray { "12 dB", "24 dB" }, 1));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_cutoff", 1 },
            "Filter Cutoff", logRange (20.0f, 20000.0f), 1000.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_reso", 1 },
            "Filter Resonance", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_drive", 1 },
            "Filter Drive", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_keytrack", 1 },
            "Filter Keytrack", juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_envAmount", 1 },
            "Filter Env Amount", juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f));

        // --- Drive / shaper (PRD §5.5) ---
        layout.add (std::make_unique<APC> (juce::ParameterID { "drive_type", 1 },
            "Drive Type", juce::StringArray { "Tanh", "Hard clip", "Foldback" }, 0));
        layout.add (std::make_unique<APF> (juce::ParameterID { "drive_amount", 1 },
            "Drive Amount", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "drive_trim", 1 },
            "Drive Trim", juce::NormalisableRange<float> { -24.0f, 6.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<APC> (juce::ParameterID { "drive_position", 1 },
            "Drive Position", juce::StringArray { "Pre-filter", "Post-filter" }, 0));

        // --- Filter envelope (PRD §6.1) ---
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_attack", 1 },
            "Filter Attack",  timeRange (10.0f), 0.005f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_decay", 1 },
            "Filter Decay",   timeRange (10.0f), 0.2f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_sustain", 1 },
            "Filter Sustain", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.8f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_release", 1 },
            "Filter Release", timeRange (15.0f), 0.3f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_velAmt", 1 },
            "Filter Env Velocity", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));

        // --- Phase 5: mod LFOs (PRD §6.2) ---
        const juce::StringArray modLfoShapes { "Sine", "Triangle", "Saw Up", "Saw Down",
                                               "Square", "S&H", "Random" };
        const juce::StringArray syncDivs { "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4", "1/4T",
                                           "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T",
                                           "1/32.", "1/32", "1/32T" };
        juce::NormalisableRange<float> modLfoRate { 0.01f, 40.0f, 0.0f };
        modLfoRate.setSkewForCentre (2.0f);

        for (int n = 1; n <= 2; ++n)
        {
            const juce::String id   = "lfo" + juce::String (n);
            const juce::String name = "LFO " + juce::String (n);
            layout.add (std::make_unique<APC> (juce::ParameterID { id + "_shape", 1 },
                name + " Shape", modLfoShapes, 0));
            layout.add (std::make_unique<APF> (juce::ParameterID { id + "_rate", 1 },
                name + " Rate", modLfoRate, 1.0f));
            layout.add (std::make_unique<APB> (juce::ParameterID { id + "_sync", 1 },
                name + " Sync", false));
            layout.add (std::make_unique<APC> (juce::ParameterID { id + "_syncDiv", 1 },
                name + " Sync Div", syncDivs, 5));                       // 1/4
            layout.add (std::make_unique<APF> (juce::ParameterID { id + "_phase", 1 },
                name + " Phase", juce::NormalisableRange<float> { 0.0f, 360.0f, 0.1f }, 0.0f));
            layout.add (std::make_unique<APF> (juce::ParameterID { id + "_fadeIn", 1 },
                name + " Fade In", juce::NormalisableRange<float> { 0.0f, 5.0f, 0.001f }, 0.0f));
            layout.add (std::make_unique<APC> (juce::ParameterID { id + "_polarity", 1 },
                name + " Polarity", juce::StringArray { "Bipolar", "Unipolar" }, 0));
            layout.add (std::make_unique<APC> (juce::ParameterID { id + "_mode", 1 },
                name + " Mode", juce::StringArray { "Poly", "Global" }, 0));
        }

        // --- Phase 5: mod env + amp velocity (PRD §6.1, §5.7) ---
        layout.add (std::make_unique<APF> (juce::ParameterID { "mod_attack", 1 },
            "Mod Attack",  timeRange (10.0f), 0.005f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "mod_decay", 1 },
            "Mod Decay",   timeRange (10.0f), 0.2f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "mod_sustain", 1 },
            "Mod Sustain", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.8f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "mod_release", 1 },
            "Mod Release", timeRange (15.0f), 0.3f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "mod_velAmt", 1 },
            "Mod Env Velocity", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "amp_velSens", 1 },
            "Amp Velocity Sens", juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f));

        // --- Phase 5: mod matrix (PRD §6.3) ---
        // Order MUST match the ModMatrix enums — they are the contract.
        static const char* const srcNames[] = { "LFO 1", "LFO 2", "Amp Env", "Filter Env",
                                                "Mod Env", "Velocity", "Mod Wheel", "Aftertouch",
                                                "Key Track", "Noise S&H", "Random" };
        static const char* const dstNames[] = { "Vector X", "Vector Y",
                                                "Osc A Pitch", "Osc B Pitch", "Osc C Pitch", "Osc D Pitch",
                                                "Osc A PW", "Osc B PW", "Osc C PW", "Osc D PW",
                                                "Osc A Level", "Osc B Level", "Osc C Level", "Osc D Level",
                                                "Sub Level", "Noise Level", "Noise Color", "Noise Cutoff",
                                                "Filter Cutoff", "Filter Reso", "Drive Amount", "Amp Level",
                                                "LFO1 Rate", "LFO2 Rate", "Pan" };
        static_assert (std::size (srcNames) == (size_t) ModMatrix::kNumSources);
        static_assert (std::size (dstNames) == (size_t) ModMatrix::kNumDests);

        const juce::StringArray srcChoices { srcNames, (int) std::size (srcNames) };
        const juce::StringArray dstChoices { dstNames, (int) std::size (dstNames) };

        for (int s = 1; s <= 8; ++s)
        {
            const juce::String id   = "mod" + juce::String (s);
            const juce::String name = "Mod " + juce::String (s);
            layout.add (std::make_unique<APC> (juce::ParameterID { id + "_src", 1 },
                name + " Source", srcChoices, 0));
            layout.add (std::make_unique<APC> (juce::ParameterID { id + "_dst", 1 },
                name + " Dest", dstChoices, 0));
            layout.add (std::make_unique<APF> (juce::ParameterID { id + "_amt", 1 },
                name + " Amount", juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f));
            layout.add (std::make_unique<APB> (juce::ParameterID { id + "_en", 1 },
                name + " Enable", false));
        }

        return layout;
    }
}
