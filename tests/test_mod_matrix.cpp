#include <catch2/catch_test_macros.hpp>
#include "dsp/mod/ModMatrix.h"

using MM = vectron::ModMatrix;

namespace
{
    void zeroSources (float (&s)[MM::kNumSources]) { for (auto& v : s) v = 0.0f; }
}

TEST_CASE ("enum sizes match the PRD contract")
{
    STATIC_REQUIRE (MM::kNumSources == 11);
    STATIC_REQUIRE (MM::kNumDests  == 26);          // PRD §6.3's 25 + Traj Depth (Phase 6 spec)
    STATIC_REQUIRE (MM::kNumSlots  == 8);
    STATIC_REQUIRE (MM::DstTrajDepth == 25);        // appended last: existing preset indices hold
}

TEST_CASE ("disabled slots contribute nothing")
{
    MM::Slot slots[MM::kNumSlots] {};
    slots[0] = { MM::SrcLfo1, MM::DstVectorX, 1.0f, false };
    float src[MM::kNumSources]; zeroSources (src);
    src[MM::SrcLfo1] = 1.0f;
    float dst[MM::kNumDests];
    MM::evaluate (slots, src, dst);
    for (auto v : dst) REQUIRE (v == 0.0f);
}

TEST_CASE ("amount scales and two slots accumulate onto one destination")
{
    MM::Slot slots[MM::kNumSlots] {};
    slots[0] = { MM::SrcLfo1,     MM::DstFilterCutoff,  0.5f,  true };
    slots[1] = { MM::SrcVelocity, MM::DstFilterCutoff, -0.25f, true };
    float src[MM::kNumSources]; zeroSources (src);
    src[MM::SrcLfo1]     = 1.0f;
    src[MM::SrcVelocity] = 0.8f;
    float dst[MM::kNumDests];
    MM::evaluate (slots, src, dst);
    REQUIRE (dst[MM::DstFilterCutoff] == 0.5f + 0.8f * -0.25f);
    REQUIRE (dst[MM::DstVectorX] == 0.0f);
}

TEST_CASE ("out-of-range ids are ignored, and the output is always zeroed first")
{
    MM::Slot slots[MM::kNumSlots] {};
    slots[0] = { 99, MM::DstPan, 1.0f, true };
    slots[1] = { MM::SrcLfo1, -3, 1.0f, true };
    float src[MM::kNumSources]; zeroSources (src);
    src[MM::SrcLfo1] = 1.0f;
    float dst[MM::kNumDests];
    for (auto& v : dst) v = 42.0f;                 // garbage in
    MM::evaluate (slots, src, dst);
    for (auto v : dst) REQUIRE (v == 0.0f);        // zeroed, nothing applied
}
