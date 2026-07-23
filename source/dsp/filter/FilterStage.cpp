#include "FilterStage.h"

void FilterStage::prepare (double sampleRate)
{
    svf.setSampleRate (sampleRate);
    ladder.prepare ({ sampleRate, 1u, 1u });   // mono, per-sample driven
    updateLadderMode();
    reset();
}

void FilterStage::setEngine (Engine e) noexcept
{
    if (engine != e)
    {
        engine = e;
        reset();                               // no stale resonant energy across the switch
    }
}

void FilterStage::setMode (Mode m) noexcept
{
    mode = m;
    svf.setMode (static_cast<SvfFilter::Mode> (m));
    updateLadderMode();
}

void FilterStage::setSlope24 (bool on) noexcept
{
    slope24 = on;
    svf.setSlope24 (on);
    updateLadderMode();
}

void FilterStage::setCutoff (float hz) noexcept
{
    if (engine == Engine::SVF)
        svf.setCutoff (hz);
    else
        ladder.setCutoffFrequencyHz (hz);
}

void FilterStage::setResonance (float r) noexcept
{
    svf.setResonance (r);
    ladder.setResonance (r);
}

void FilterStage::setDrive (float d) noexcept
{
    svf.setDrive (d);
    ladder.setDrive (1.0f + 9.0f * d);         // juce Ladder drive: >= 1
}

void FilterStage::reset() noexcept
{
    svf.reset();
    ladder.reset();
}

float FilterStage::processSample (float x) noexcept
{
    if (engine == Engine::SVF)
        return svf.processSample (x);

    ladder.updateSmoothers();
    return ladder.processSample (x, 0);
}

void FilterStage::updateLadderMode() noexcept
{
    using LM = juce::dsp::LadderFilterMode;
    LM lm = LM::LPF24;
    switch (mode)
    {
        case Mode::LP:                     lm = slope24 ? LM::LPF24 : LM::LPF12; break;
        case Mode::BP:  case Mode::Notch:  lm = slope24 ? LM::BPF24 : LM::BPF12; break;  // Notch -> BP fallback
        case Mode::HP:                     lm = slope24 ? LM::HPF24 : LM::HPF12; break;
    }
    ladder.setMode (lm);
}
