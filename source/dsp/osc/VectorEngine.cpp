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
