#include "VectorEngine.h"
#include <cmath>

VectorEngine::Weights VectorEngine::computeWeights (float x, float y, Xfade mode) noexcept
{
    const float u = 0.5f * (x + 1.0f);   // [0,1]
    const float v = 0.5f * (y + 1.0f);

    float gA = (1.0f - u) * v;            // top-left
    float gB = u * v;                     // top-right
    float gC = (1.0f - u) * (1.0f - v);   // bottom-left
    float gD = u * (1.0f - v);            // bottom-right

    if (mode == Xfade::EqualPower)
    {
        gA = std::sqrt (gA); gB = std::sqrt (gB);
        gC = std::sqrt (gC); gD = std::sqrt (gD);
        const float norm = gA + gB + gC + gD;
        if (norm > 0.0f)
        {
            const float inv = 1.0f / norm;
            gA *= inv; gB *= inv; gC *= inv; gD *= inv;
        }
    }
    return { gA, gB, gC, gD };
}

void VectorEngine::setSampleRate (double sr) noexcept
{
    for (auto& o : osc) o.setSampleRate (sr);
}

void VectorEngine::setWave (int idx, PolyBlepOscillator::Wave w) noexcept { osc[idx].setWave (w); }
void VectorEngine::setLevel (int idx, float l) noexcept { level[idx] = l; }
void VectorEngine::setPulseWidth (int idx, float pw) noexcept { osc[idx].setPulseWidth (pw); }
void VectorEngine::setPhaseResetEnabled (int idx, bool e) noexcept { phaseReset[idx] = e; }
void VectorEngine::setXfadeMode (Xfade mode) noexcept { xfade = mode; }

void VectorEngine::setDetune (int idx, int oct, int coarseSemis, float fineCents) noexcept
{
    octave[idx] = oct;
    coarse[idx] = coarseSemis;
    fine[idx]   = fineCents;
    updateFrequency (idx);
}

void VectorEngine::setNoteFrequency (float hz) noexcept
{
    baseHz = hz;
    for (int i = 0; i < kNumOsc; ++i) updateFrequency (i);
}

void VectorEngine::updateFrequency (int idx) noexcept
{
    const float semis = (float) octave[idx] * 12.0f + (float) coarse[idx] + fine[idx] * 0.01f;
    osc[idx].setFrequency (baseHz * std::pow (2.0f, semis / 12.0f));
}

void VectorEngine::noteOn() noexcept
{
    for (int i = 0; i < kNumOsc; ++i)
        if (phaseReset[i]) osc[i].reset (0.0f);
}

void VectorEngine::setVectorPosition (float x, float y) noexcept
{
    weight = computeWeights (x, y, xfade);
}

float VectorEngine::processSample() noexcept
{
    const float a = osc[0].processSample();   // advance ALL — free-running oscs keep phase
    const float b = osc[1].processSample();
    const float c = osc[2].processSample();
    const float d = osc[3].processSample();
    return weight.a * a * level[0]
         + weight.b * b * level[1]
         + weight.c * c * level[2]
         + weight.d * d * level[3];
}
