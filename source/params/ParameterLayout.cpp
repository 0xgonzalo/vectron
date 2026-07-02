#include "ParameterLayout.h"

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

        return layout;
    }
}
