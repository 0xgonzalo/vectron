#pragma once

// 8-slot modulation matrix (PRD §6.3). The enums below ARE the contract:
// APVTS choice-array order and the voice's destination handling must match.
// Pure accumulate step; clamping happens at the application site. JUCE-free.
namespace vectron
{
struct ModMatrix
{
    enum Source : int
    {
        SrcLfo1, SrcLfo2, SrcAmpEnv, SrcFilterEnv, SrcModEnv, SrcVelocity,
        SrcModWheel, SrcAftertouch, SrcKeyTrack, SrcNoiseSH, SrcRandomNote,
        kNumSources
    };

    enum Dest : int
    {
        DstVectorX, DstVectorY,
        DstOscAPitch, DstOscBPitch, DstOscCPitch, DstOscDPitch,
        DstOscAPw, DstOscBPw, DstOscCPw, DstOscDPw,
        DstOscALevel, DstOscBLevel, DstOscCLevel, DstOscDLevel,
        DstSubLevel, DstNoiseLevel, DstNoiseColor, DstNoiseCutoff,
        DstFilterCutoff, DstFilterReso, DstDriveAmount, DstAmpLevel,
        DstLfo1Rate, DstLfo2Rate, DstPan,
        DstTrajDepth,                       // Phase 6: appended last so preset indices hold
        kNumDests
    };

    static constexpr int kNumSlots = 8;

    struct Slot
    {
        int   source  = SrcLfo1;
        int   dest    = DstVectorX;
        float amount  = 0.0f;      // -1 .. +1
        bool  enabled = false;
    };

    static void evaluate (const Slot (&slots)[kNumSlots],
                          const float (&sources)[kNumSources],
                          float (&dests)[kNumDests]) noexcept
    {
        for (auto& d : dests) d = 0.0f;
        for (const auto& s : slots)
            if (s.enabled
                && s.source >= 0 && s.source < kNumSources
                && s.dest   >= 0 && s.dest   < kNumDests)
                dests[s.dest] += sources[s.source] * s.amount;
    }
};
}
