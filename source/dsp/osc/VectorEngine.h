#pragma once
#include "PolyBlepOscillator.h"

class VectorEngine
{
public:
    enum class Xfade { Linear, EqualPower };
    struct Weights { float a, b, c, d; };

    static Weights computeWeights (float x, float y, Xfade mode) noexcept;
};
