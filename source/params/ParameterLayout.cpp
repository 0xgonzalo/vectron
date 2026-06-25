#include "ParameterLayout.h"

namespace vectron
{
    using APF = juce::AudioParameterFloat;

    static juce::NormalisableRange<float> timeRange (float maxSeconds)
    {
        juce::NormalisableRange<float> r { 0.0f, maxSeconds, 0.0001f };
        r.setSkewForCentre (maxSeconds * 0.15f);   // log-ish: more resolution low
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

        return layout;
    }
}
