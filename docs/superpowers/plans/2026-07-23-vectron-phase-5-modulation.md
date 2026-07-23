# Vectron Phase 5 — Modulation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two full-featured LFOs (poly/global, tempo-syncable), a free Mod Env, an 8-slot mod matrix (11 sources × 25 destinations), and migration of all three envelopes to an exponential-segment `AdsrEnvelope`.

**Architecture:** Three new JUCE-free header-only DSP units (`AdsrEnvelope`, `ModLfo`, `ModMatrix`) unit-tested in isolation; `VectronVoice` evaluates the matrix per sample and applies clamped offsets; `VectronProcessor` resolves tempo sync and advances master LFO phases for Global mode. Spec: `docs/superpowers/specs/2026-07-23-vectron-phase-5-modulation-design.md`.

**Tech Stack:** JUCE 8.0.13, C++20, CMake, Catch2 v3.7.1.

## Global Constraints

- JUCE-free DSP: `AdsrEnvelope.h`, `ModLfo.h`, `ModMatrix.h` include **no** JUCE headers.
- No allocations/locks in `processBlock` or voice render.
- Param IDs `snake_case`; choice option order is load-bearing (matches enums).
- Build dir: `build/`; tests: `cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure` (or run the binary directly).
- Branch: `feat/phase-5-modulation`.

---

### Task 1: AdsrEnvelope (exponential ADSR)

**Files:**
- Create: `source/dsp/mod/AdsrEnvelope.h`
- Test: `tests/test_adsr_envelope.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

**Interfaces:**
- Produces: `AdsrEnvelope` with `struct Parameters { float attack, decay, sustain, release; }`, `setSampleRate(double)`, `setParameters(const Parameters&)`, `noteOn()`, `noteOff()`, `reset()`, `float getNextSample()`, `bool isActive() const`, `float getCurrentValue() const`. Used by Task 6 as a drop-in for `juce::ADSR`.

- [ ] **Step 1: Write the failing test** — `tests/test_adsr_envelope.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/mod/AdsrEnvelope.h"

namespace
{
    AdsrEnvelope make (float a, float d, float s, float r, double sr = 48000.0)
    {
        AdsrEnvelope env;
        env.setSampleRate (sr);
        env.setParameters ({ a, d, s, r });
        return env;
    }

    // advance n samples, return last value
    float run (AdsrEnvelope& env, int n)
    {
        float v = 0.0f;
        for (int i = 0; i < n; ++i) v = env.getNextSample();
        return v;
    }
}

TEST_CASE ("attack reaches full level within the attack time")
{
    auto env = make (0.1f, 0.2f, 0.5f, 0.1f);
    env.noteOn();
    REQUIRE (run (env, 4800) >= 0.99f);          // 0.1 s @ 48 kHz
}

TEST_CASE ("attack segment is exponential (convex: midpoint above linear)")
{
    auto env = make (0.1f, 0.2f, 0.5f, 0.1f);
    env.noteOn();
    const float mid = run (env, 2400);           // half the attack time
    REQUIRE (mid > 0.55f);                       // linear would be ~0.5
}

TEST_CASE ("decay is exponential (concave: midpoint below linear) and lands on sustain")
{
    auto env = make (0.0f, 0.2f, 0.4f, 0.1f);
    env.noteOn();
    (void) env.getNextSample();                  // instant attack -> 1.0
    const float mid = run (env, 4800);           // half the decay time
    REQUIRE (mid < 0.68f);                       // linear midpoint would be ~0.7
    REQUIRE (std::abs (run (env, 9600) - 0.4f) < 0.01f);
}

TEST_CASE ("sustain holds until noteOff, then release decays to inactive")
{
    auto env = make (0.0f, 0.01f, 0.6f, 0.05f);
    env.noteOn();
    run (env, 4800);
    REQUIRE (std::abs (env.getCurrentValue() - 0.6f) < 0.01f);
    env.noteOff();
    run (env, 48000 / 10);                        // 0.1 s >> 0.05 s release
    REQUIRE (env.getCurrentValue() < 1.0e-3f);
    REQUIRE_FALSE (env.isActive());
}

TEST_CASE ("retrigger is click-free: attack restarts from the current value")
{
    auto env = make (0.5f, 0.2f, 0.8f, 0.5f);
    env.noteOn();
    run (env, 10000);
    const float before = env.getCurrentValue();
    env.noteOn();                                 // retrigger mid-attack
    const float after = env.getNextSample();
    REQUIRE (std::abs (after - before) < 0.01f);
}

TEST_CASE ("zero-length segments are instant and stable")
{
    auto env = make (0.0f, 0.0f, 0.5f, 0.0f);
    env.noteOn();
    (void) env.getNextSample();                   // attack -> 1
    REQUIRE (std::abs (run (env, 2) - 0.5f) < 0.01f);   // decay -> sustain
    env.noteOff();
    (void) env.getNextSample();
    REQUIRE (env.getCurrentValue() < 1.0e-3f);
    REQUIRE_FALSE (env.isActive());
}

TEST_CASE ("idle envelope outputs zero and reports inactive")
{
    auto env = make (0.1f, 0.1f, 0.5f, 0.1f);
    REQUIRE (env.getNextSample() == 0.0f);
    REQUIRE_FALSE (env.isActive());
}
```

Add `test_adsr_envelope.cpp` to `add_executable(VectronTests ...)` in `tests/CMakeLists.txt` (after `test_filter_math.cpp`).

- [ ] **Step 2: Run to verify it fails** — `cmake --build build --target VectronTests` → compile error: `AdsrEnvelope.h` not found.

- [ ] **Step 3: Implement** — `source/dsp/mod/AdsrEnvelope.h`:

```cpp
#pragma once
#include <cmath>

// Exponential-segment ADSR (one-pole toward overshoot targets, EarLevel/Redmon
// style). API mirrors juce::ADSR so the voice swaps it in place; also readable
// mid-block as a mod source via getCurrentValue(). JUCE-free.
class AdsrEnvelope
{
public:
    struct Parameters
    {
        float attack  = 0.005f;   // seconds
        float decay   = 0.2f;
        float sustain = 0.8f;     // level 0..1
        float release = 0.3f;
    };

    void setSampleRate (double sr) noexcept
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        recalc();
    }

    void setParameters (const Parameters& p) noexcept
    {
        params = p;
        recalc();
    }

    void reset() noexcept   { state = State::Idle; value = 0.0f; }
    void noteOn() noexcept  { state = State::Attack; }                       // from current value: no click
    void noteOff() noexcept { if (state != State::Idle) state = State::Release; }

    bool  isActive() const noexcept        { return state != State::Idle; }
    float getCurrentValue() const noexcept { return value; }

    float getNextSample() noexcept
    {
        switch (state)
        {
            case State::Attack:
                value = attackTarget + (value - attackTarget) * attackCoef;
                if (value >= 1.0f) { value = 1.0f; state = State::Decay; }
                break;
            case State::Decay:
                value = decayTarget + (value - decayTarget) * decayCoef;
                if (value <= params.sustain) { value = params.sustain; state = State::Sustain; }
                break;
            case State::Sustain:
                value = params.sustain;                                      // tracks knob movement
                break;
            case State::Release:
                value = releaseTarget + (value - releaseTarget) * releaseCoef;
                if (value <= kSilence) { value = 0.0f; state = State::Idle; }
                break;
            case State::Idle:
                value = 0.0f;
                break;
        }
        return value;
    }

private:
    enum class State { Idle, Attack, Decay, Sustain, Release };

    static constexpr float kSilence     = 1.0e-4f;
    static constexpr float kAttackRatio = 0.3f;     // overshoot above 1.0 -> convex attack
    static constexpr float kDecayRatio  = 1.0e-4f;  // undershoot below target -> fast-settling tail

    // One-pole coefficient that traverses the segment in ~timeSec seconds.
    float coefFor (float timeSec, float ratio) const noexcept
    {
        const float samples = timeSec * (float) sampleRate;
        if (samples < 1.0f) return 0.0f;                                     // instant
        return std::exp (-std::log ((1.0f + ratio) / ratio) / samples);
    }

    void recalc() noexcept
    {
        attackCoef    = coefFor (params.attack,  kAttackRatio);
        decayCoef     = coefFor (params.decay,   kDecayRatio);
        releaseCoef   = coefFor (params.release, kDecayRatio);
        attackTarget  = 1.0f + kAttackRatio;
        decayTarget   = params.sustain - kDecayRatio;
        releaseTarget = -kDecayRatio;
    }

    Parameters params;
    double sampleRate = 44100.0;
    float  value = 0.0f;
    float  attackCoef = 0.0f, decayCoef = 0.0f, releaseCoef = 0.0f;
    float  attackTarget = 1.3f, decayTarget = 0.0f, releaseTarget = 0.0f;
    State  state = State::Idle;
};
```

- [ ] **Step 4: Run tests** — build `VectronTests`, run `./build/tests/VectronTests "[adsr]"` or full binary. Expected: all pass (also re-run full suite: no regressions).
- [ ] **Step 5: Commit** — `git add source/dsp/mod/AdsrEnvelope.h tests/test_adsr_envelope.cpp tests/CMakeLists.txt && git commit -m "feat: exponential AdsrEnvelope (PRD §6.1) + unit tests"`

---

### Task 2: ModLfo (7-shape LFO, hash-based randoms, absolute phase)

**Files:**
- Create: `source/dsp/mod/ModLfo.h`
- Test: `tests/test_mod_lfo.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `ModLfo` with `enum class Shape { Sine, Triangle, SawUp, SawDown, Square, SampleHold, RandomSmooth }`, `enum class Polarity { Bipolar, Unipolar }`, `setSampleRate(double)`, `setRate(float hz)`, `setShape(Shape)`, `setPolarity(Polarity)`, `setPhaseOffsetDegrees(float)`, `setFadeInSeconds(float)`, `setSeed(uint32_t)`, `retrigger()`, `startFadeIn()`, `setAbsolutePhase(double cycles)`, `double getPhaseIncrement() const`, `float processSample()`. Used by Task 6.

- [ ] **Step 1: Write the failing test** — `tests/test_mod_lfo.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/mod/ModLfo.h"

namespace
{
    ModLfo make (ModLfo::Shape s, float rate = 1.0f, double sr = 1000.0)
    {
        ModLfo l;
        l.setSampleRate (sr);
        l.setRate (rate);
        l.setShape (s);
        l.retrigger();
        return l;
    }
}

TEST_CASE ("saw up traverses -1..1 over one cycle")
{
    auto l = make (ModLfo::Shape::SawUp);          // 1 Hz @ 1 kHz -> 1000 samples/cycle
    REQUIRE (std::abs (l.processSample() - (-1.0f)) < 1.0e-4f);   // phase 0
    for (int i = 0; i < 499; ++i) (void) l.processSample();
    REQUIRE (std::abs (l.processSample() - 0.0f) < 5.0e-3f);      // phase 0.5
}

TEST_CASE ("sine hits known values and square flips at half phase")
{
    auto sine = make (ModLfo::Shape::Sine);
    REQUIRE (std::abs (sine.processSample()) < 1.0e-4f);          // sin(0)
    for (int i = 0; i < 249; ++i) (void) sine.processSample();
    REQUIRE (sine.processSample() > 0.99f);                        // sin(pi/2)

    auto sq = make (ModLfo::Shape::Square);
    REQUIRE (sq.processSample() == 1.0f);
    for (int i = 0; i < 499; ++i) (void) sq.processSample();
    REQUIRE (sq.processSample() == -1.0f);
}

TEST_CASE ("phase offset shifts the start point")
{
    ModLfo l;
    l.setSampleRate (1000.0);
    l.setRate (1.0f);
    l.setShape (ModLfo::Shape::SawUp);
    l.setPhaseOffsetDegrees (180.0f);
    l.retrigger();
    REQUIRE (std::abs (l.processSample() - 0.0f) < 5.0e-3f);      // saw at phase 0.5
}

TEST_CASE ("unipolar output stays in [0,1]")
{
    auto l = make (ModLfo::Shape::Sine, 7.0f);
    l.setPolarity (ModLfo::Polarity::Unipolar);
    for (int i = 0; i < 5000; ++i)
    {
        const float v = l.processSample();
        REQUIRE (v >= 0.0f);
        REQUIRE (v <= 1.0f);
    }
}

TEST_CASE ("fade-in ramps amplitude over the configured time")
{
    ModLfo l;
    l.setSampleRate (1000.0);
    l.setRate (0.0f);                             // freeze phase at 0.25 -> constant +1 for sine? use square
    l.setShape (ModLfo::Shape::Square);           // value +1 at phase 0
    l.setFadeInSeconds (1.0f);
    l.retrigger();
    (void) l.processSample();                     // fade starts near 0
    for (int i = 0; i < 498; ++i) (void) l.processSample();
    const float half = l.processSample();
    REQUIRE (half > 0.4f);
    REQUIRE (half < 0.6f);                        // ~halfway through the fade
    for (int i = 0; i < 600; ++i) (void) l.processSample();
    REQUIRE (l.processSample() == 1.0f);          // fade complete
}

TEST_CASE ("sample & hold is constant within a cycle, deterministic per cycle index")
{
    auto a = make (ModLfo::Shape::SampleHold, 1.0f);
    const float first = a.processSample();
    for (int i = 0; i < 900; ++i)
        REQUIRE (a.processSample() == first);     // same cycle -> held

    auto b = make (ModLfo::Shape::SampleHold, 1.0f);
    REQUIRE (b.processSample() == first);         // same seed + cycle -> same value
}

TEST_CASE ("random smooth is continuous (bounded per-sample delta)")
{
    auto l = make (ModLfo::Shape::RandomSmooth, 5.0f);
    float prev = l.processSample();
    for (int i = 0; i < 3000; ++i)
    {
        const float v = l.processSample();
        REQUIRE (std::abs (v - prev) < 0.1f);
        prev = v;
    }
}

TEST_CASE ("setAbsolutePhase reproduces an identical stream in a second instance")
{
    auto a = make (ModLfo::Shape::RandomSmooth, 3.0f);
    for (int i = 0; i < 777; ++i) (void) a.processSample();

    auto b = make (ModLfo::Shape::RandomSmooth, 3.0f);
    b.setAbsolutePhase (777.0 * a.getPhaseIncrement());
    // Tolerance: 'a' accumulated phase by repeated addition, 'b' jumped by a
    // product — identical to within float rounding, not bitwise.
    for (int i = 0; i < 500; ++i)
        REQUIRE (std::abs (a.processSample() - b.processSample()) < 1.0e-4f);
}
```

Add `test_mod_lfo.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails** — compile error: `ModLfo.h` not found.

- [ ] **Step 3: Implement** — `source/dsp/mod/ModLfo.h`:

```cpp
#pragma once
#include <cmath>
#include <cstdint>

// Full-featured mod LFO (PRD §6.2). Phase is an unwrapped double in cycles;
// S&H / Random-smooth derive their random values from a hash of the cycle
// index, so output is a pure function of phase — deterministic across voices
// in Global mode and safe under absolute-phase jumps. JUCE-free.
class ModLfo
{
public:
    enum class Shape    { Sine, Triangle, SawUp, SawDown, Square, SampleHold, RandomSmooth };
    enum class Polarity { Bipolar, Unipolar };

    void setSampleRate (double sr) noexcept        { sampleRate = sr > 0.0 ? sr : 44100.0; recalc(); }
    void setRate (float hz) noexcept               { rateHz = hz; recalc(); }
    void setShape (Shape s) noexcept               { shape = s; }
    void setPolarity (Polarity p) noexcept         { polarity = p; }
    void setPhaseOffsetDegrees (float deg) noexcept{ phaseOffset = deg / 360.0f; }
    void setFadeInSeconds (float s) noexcept       { fadeSeconds = s; recalc(); }
    void setSeed (uint32_t s) noexcept             { seed = s; }

    double getPhaseIncrement() const noexcept      { return increment; }

    // Poly-mode note-on: restart phase at the offset and restart the fade.
    void retrigger() noexcept      { phase = (double) phaseOffset; startFadeIn(); }
    // Global-mode note-on: fade restarts, phase untouched.
    void startFadeIn() noexcept    { fade = fadeInc >= 1.0f ? 1.0f : 0.0f; }
    // Global-mode block sync: absolute master phase in cycles (unwrapped).
    void setAbsolutePhase (double cycles) noexcept { phase = cycles + (double) phaseOffset; }

    float processSample() noexcept
    {
        const float v = valueAt (phase) * fade;
        phase += increment;
        fade += fadeInc;
        if (fade > 1.0f) fade = 1.0f;
        return polarity == Polarity::Unipolar ? 0.5f * (v + 1.0f) : v;
    }

private:
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr float kPi    = 3.1415926535897932f;

    static uint32_t hash (uint64_t x) noexcept              // splitmix64 finalizer
    {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return (uint32_t) (x ^ (x >> 31));
    }

    float randForCycle (int64_t k) const noexcept           // [-1, 1]
    {
        const uint32_t h = hash ((uint64_t) k ^ ((uint64_t) seed << 32));
        return (float) (h & 0xFFFFFFu) / 16777215.0f * 2.0f - 1.0f;
    }

    float valueAt (double ph) const noexcept
    {
        const auto  cycle = (int64_t) std::floor (ph);
        const float t     = (float) (ph - (double) cycle);   // fractional phase [0,1)
        switch (shape)
        {
            case Shape::Sine:       return std::sin (kTwoPi * t);
            case Shape::Triangle:   return t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
            case Shape::SawUp:      return 2.0f * t - 1.0f;
            case Shape::SawDown:    return 1.0f - 2.0f * t;
            case Shape::Square:     return t < 0.5f ? 1.0f : -1.0f;
            case Shape::SampleHold: return randForCycle (cycle);
            case Shape::RandomSmooth:
            {
                const float a = randForCycle (cycle);
                const float b = randForCycle (cycle + 1);
                const float e = 0.5f - 0.5f * std::cos (kPi * t);   // cosine ease
                return a + (b - a) * e;
            }
        }
        return 0.0f;
    }

    void recalc() noexcept
    {
        increment = (double) rateHz / sampleRate;
        fadeInc   = fadeSeconds > 1.0e-4f ? (float) (1.0 / (fadeSeconds * sampleRate)) : 2.0f;
    }

    double   sampleRate  = 44100.0;
    double   phase       = 0.0;      // cycles, unwrapped
    double   increment   = 0.0;
    float    rateHz      = 1.0f;
    float    phaseOffset = 0.0f;     // cycles
    float    fadeSeconds = 0.0f;
    float    fade        = 1.0f;
    float    fadeInc     = 2.0f;     // >= 1 -> no fade
    uint32_t seed        = 1u;
    Shape    shape       = Shape::Sine;
    Polarity polarity    = Polarity::Bipolar;
};
```

- [ ] **Step 4: Run tests** — full `VectronTests` suite passes.
- [ ] **Step 5: Commit** — `git commit -m "feat: ModLfo — 7 shapes, fade-in, polarity, hash-based randoms, absolute phase (PRD §6.2)"`

---

### Task 3: ModMatrix (pure evaluate)

**Files:**
- Create: `source/dsp/mod/ModMatrix.h`
- Test: `tests/test_mod_matrix.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `vectron::ModMatrix` with `enum Source {..., kNumSources = 11}`, `enum Dest {..., kNumDests = 25}`, `kNumSlots = 8`, `struct Slot { int source; int dest; float amount; bool enabled; }`, `static void evaluate(const Slot(&)[8], const float(&)[kNumSources], float(&)[kNumDests])`. Enum order is the APVTS choice order (Task 5) and the voice's application order (Task 6).

- [ ] **Step 1: Write the failing test** — `tests/test_mod_matrix.cpp`:

```cpp
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
    STATIC_REQUIRE (MM::kNumDests  == 25);
    STATIC_REQUIRE (MM::kNumSlots  == 8);
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
    slots[0] = { MM::SrcLfo1,     MM::DstFilterCutoff,  0.5f, true };
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
```

Add `test_mod_matrix.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails** — compile error: `ModMatrix.h` not found.

- [ ] **Step 3: Implement** — `source/dsp/mod/ModMatrix.h`:

```cpp
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
```

- [ ] **Step 4: Run tests** — full suite passes.
- [ ] **Step 5: Commit** — `git commit -m "feat: ModMatrix — 8 slots, 11 sources x 25 dests, pure evaluate (PRD §6.3)"`

---

### Task 4: FilterMath modOct + VectorEngine pitch mod

**Files:**
- Modify: `source/dsp/filter/FilterMath.h`, `source/dsp/osc/VectorEngine.h`, `source/dsp/osc/VectorEngine.cpp`
- Test: `tests/test_filter_math.cpp`, `tests/test_vector_engine.cpp` (append cases)

**Interfaces:**
- Produces: `effectiveCutoffHz(baseHz, midiNote, keytrackPct, env, envAmount, float modOct = 0.0f)` — existing five-arg calls stay valid. `VectorEngine::setPitchModSemis(int idx, float semis)` — additive semitone offset per oscillator, cheap no-op when unchanged (< 1e-4 st).

- [ ] **Step 1: Write the failing tests.** Append to `tests/test_filter_math.cpp`:

```cpp
TEST_CASE ("matrix modOct adds octaves on top of keytrack + env, still clamped")
{
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60, 0.0f, 0.0f, 0.0f,  1.0f), WithinRel (2000.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60, 0.0f, 0.0f, 0.0f, -2.0f), WithinRel ( 250.0f, 1e-5f));
    REQUIRE (effectiveCutoffHz (1000.0f, 60, 0.0f, 0.0f, 0.0f,  10.0f) == 20000.0f);
    REQUIRE (effectiveCutoffHz (1000.0f, 60, 0.0f, 1.0f, 1.0f, -10.0f) == 20.0f);
}
```

Append to `tests/test_vector_engine.cpp` (match the file's existing include/using style):

```cpp
TEST_CASE ("setPitchModSemis shifts one oscillator's frequency")
{
    VectorEngine e;
    e.setSampleRate (48000.0);
    e.setWave (0, PolyBlepOscillator::Wave::Sine);
    e.setNoteFrequency (440.0f);
    e.setVectorPosition (-1.0f, 1.0f);            // full weight on osc A
    e.noteOn();

    // Measure the period of osc A via zero crossings, +12 st must double them.
    auto countCrossings = [] (VectorEngine& eng, int n)
    {
        int crossings = 0;
        float prev = eng.processSample();
        for (int i = 1; i < n; ++i)
        {
            const float v = eng.processSample();
            if (prev <= 0.0f && v > 0.0f) ++crossings;
            prev = v;
        }
        return crossings;
    };

    const int base = countCrossings (e, 48000);   // ~440 crossings in 1 s
    e.setPitchModSemis (0, 12.0f);
    e.noteOn();
    const int up = countCrossings (e, 48000);     // ~880
    REQUIRE (up > base * 3 / 2);
    REQUIRE (std::abs (up - 2 * base) <= base / 10);
}
```

- [ ] **Step 2: Run to verify failure** — `setPitchModSemis` undeclared; modOct test fails to compile only if arity strict — six-arg call fails until default param added.

- [ ] **Step 3: Implement.**
`FilterMath.h` — change the signature and exponent:

```cpp
    inline float effectiveCutoffHz (float baseHz, int midiNote, float keytrackPct,
                                    float env, float envAmount, float modOct = 0.0f) noexcept
    {
        constexpr float kEnvOctaves = 5.0f;
        const float keyOct = (keytrackPct * 0.01f) * (float) (midiNote - 60) / 12.0f;
        const float envOct = env * envAmount * kEnvOctaves;
        return std::clamp (baseHz * std::exp2 (keyOct + envOct + modOct), 20.0f, 20000.0f);
    }
```

`VectorEngine.h` — add to public API and members:

```cpp
    void setPitchModSemis (int idx, float semis) noexcept;
    ...
private:
    float pitchMod[kNumOsc] { 0.0f, 0.0f, 0.0f, 0.0f };
```

`VectorEngine.cpp` — implement and fold into `updateFrequency`:

```cpp
void VectorEngine::setPitchModSemis (int idx, float semis) noexcept
{
    if (std::abs (semis - pitchMod[idx]) < 1.0e-4f)
        return;                                    // skip the pow when unchanged
    pitchMod[idx] = semis;
    updateFrequency (idx);
}

void VectorEngine::updateFrequency (int idx) noexcept
{
    const float semis = (float) octave[idx] * 12.0f + (float) coarse[idx]
                      + fine[idx] * 0.01f + pitchMod[idx];
    osc[idx].setFrequency (baseHz * std::pow (2.0f, semis / 12.0f));
}
```

- [ ] **Step 4: Run tests** — full suite passes.
- [ ] **Step 5: Commit** — `git commit -m "feat: matrix cutoff octaves in effectiveCutoffHz + VectorEngine per-osc pitch mod"`

---

### Task 5: APVTS parameters + processor caching

**Files:**
- Modify: `source/params/ParameterLayout.cpp`, `source/PluginProcessor.h`, `source/PluginProcessor.cpp`

**Interfaces:**
- Produces param IDs (Task 6/7 read them): `lfo{1,2}_{shape,rate,sync,syncDiv,phase,fadeIn,polarity,mode}`, `mod_{attack,decay,sustain,release,velAmt}`, `amp_velSens`, `mod{1..8}_{src,dst,amt,en}`. Choice arrays in `ModMatrix` enum order.

- [ ] **Step 1: Add params to `ParameterLayout.cpp`** (inside `createParameterLayout`, before `return layout;`). Include `"dsp/mod/ModMatrix.h"` at the top (JUCE-free header, fine here).

```cpp
        // --- Phase 5: mod LFOs -------------------------------------------------
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

        // --- Phase 5: mod env + amp velocity ----------------------------------
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

        // --- Phase 5: mod matrix ----------------------------------------------
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
        static_assert (std::size (srcNames) == (size_t) vectron::ModMatrix::kNumSources);
        static_assert (std::size (dstNames) == (size_t) vectron::ModMatrix::kNumDests);

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
```

Also add `#include <iterator>` if `std::size` needs it (usually pulled in already).

- [ ] **Step 2: Cache pointers in `PluginProcessor.h`** (private members, after the Phase 4 block):

```cpp
    // Phase 5: mod LFOs [lfo1=0, lfo2=1]
    std::atomic<float>* pModLfoShape[2]    { nullptr, nullptr };
    std::atomic<float>* pModLfoRate[2]     { nullptr, nullptr };
    std::atomic<float>* pModLfoSync[2]     { nullptr, nullptr };
    std::atomic<float>* pModLfoSyncDiv[2]  { nullptr, nullptr };
    std::atomic<float>* pModLfoPhase[2]    { nullptr, nullptr };
    std::atomic<float>* pModLfoFadeIn[2]   { nullptr, nullptr };
    std::atomic<float>* pModLfoPolarity[2] { nullptr, nullptr };
    std::atomic<float>* pModLfoMode[2]     { nullptr, nullptr };

    // Phase 5: mod env + amp velocity
    std::atomic<float>* pModAttack  { nullptr };
    std::atomic<float>* pModDecay   { nullptr };
    std::atomic<float>* pModSustain { nullptr };
    std::atomic<float>* pModRelease { nullptr };
    std::atomic<float>* pModVelAmt  { nullptr };
    std::atomic<float>* pAmpVelSens { nullptr };

    // Phase 5: mod matrix slots
    std::atomic<float>* pModSrc[8] {};
    std::atomic<float>* pModDst[8] {};
    std::atomic<float>* pModAmt[8] {};
    std::atomic<float>* pModEn[8]  {};

    // Global-mode LFO master phase accumulators (cycles, unwrapped; audio thread only)
    double masterLfoPhase[2] { 0.0, 0.0 };
```

- [ ] **Step 3: Resolve them in the constructor** (`PluginProcessor.cpp`, after the Phase 4 block) and extend the `jassert` block:

```cpp
    for (int n = 0; n < 2; ++n)
    {
        const juce::String id = "lfo" + juce::String (n + 1);
        pModLfoShape[n]    = apvts.getRawParameterValue (id + "_shape");
        pModLfoRate[n]     = apvts.getRawParameterValue (id + "_rate");
        pModLfoSync[n]     = apvts.getRawParameterValue (id + "_sync");
        pModLfoSyncDiv[n]  = apvts.getRawParameterValue (id + "_syncDiv");
        pModLfoPhase[n]    = apvts.getRawParameterValue (id + "_phase");
        pModLfoFadeIn[n]   = apvts.getRawParameterValue (id + "_fadeIn");
        pModLfoPolarity[n] = apvts.getRawParameterValue (id + "_polarity");
        pModLfoMode[n]     = apvts.getRawParameterValue (id + "_mode");
    }

    pModAttack  = apvts.getRawParameterValue ("mod_attack");
    pModDecay   = apvts.getRawParameterValue ("mod_decay");
    pModSustain = apvts.getRawParameterValue ("mod_sustain");
    pModRelease = apvts.getRawParameterValue ("mod_release");
    pModVelAmt  = apvts.getRawParameterValue ("mod_velAmt");
    pAmpVelSens = apvts.getRawParameterValue ("amp_velSens");

    for (int s = 0; s < 8; ++s)
    {
        const juce::String id = "mod" + juce::String (s + 1);
        pModSrc[s] = apvts.getRawParameterValue (id + "_src");
        pModDst[s] = apvts.getRawParameterValue (id + "_dst");
        pModAmt[s] = apvts.getRawParameterValue (id + "_amt");
        pModEn[s]  = apvts.getRawParameterValue (id + "_en");
    }

    for (int n = 0; n < 2; ++n)
        jassert (pModLfoShape[n] && pModLfoRate[n] && pModLfoSync[n] && pModLfoSyncDiv[n]
                 && pModLfoPhase[n] && pModLfoFadeIn[n] && pModLfoPolarity[n] && pModLfoMode[n]);
    jassert (pModAttack && pModDecay && pModSustain && pModRelease && pModVelAmt && pAmpVelSens);
    for (int s = 0; s < 8; ++s)
        jassert (pModSrc[s] && pModDst[s] && pModAmt[s] && pModEn[s]);
```

- [ ] **Step 4: Build the plugin** — `cmake --build build` → compiles; open Standalone briefly if convenient (generic editor shows the new params). All tests still pass.
- [ ] **Step 5: Commit** — `git commit -m "feat: Phase 5 parameters — LFO x2, mod env, amp velSens, 8 matrix slots (PRD §8)"`

---

### Task 6: Voice + processor integration

**Files:**
- Modify: `source/dsp/VectronVoice.h`, `source/dsp/VectronVoice.cpp`, `source/PluginProcessor.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–5.
- Produces: `VectronVoiceParams` gains fields listed below; `VectronVoice::setModAdsr(const AdsrEnvelope::Parameters&)`; `setAmpAdsr`/`setFiltAdsr` now take `AdsrEnvelope::Parameters`.

- [ ] **Step 1: Extend `VectronVoiceParams`** (`VectronVoice.h`) — append after `filtVelAmt`:

```cpp
    // Phase 5: mod LFOs [lfo1=0, lfo2=1]
    int    modLfoShape[2]       { 0, 0 };
    float  modLfoRateHz[2]      { 1.0f, 1.0f };   // tempo-resolved by the processor
    float  modLfoPhaseDeg[2]    { 0.0f, 0.0f };
    float  modLfoFadeIn[2]      { 0.0f, 0.0f };
    int    modLfoPolarity[2]    { 0, 0 };
    bool   modLfoGlobal[2]      { false, false };
    double modLfoMasterPhase[2] { 0.0, 0.0 };     // absolute phase at block start (Global mode)

    // Phase 5: mod env velocity + amp velocity sensitivity
    float modVelAmt  { 0.0f };
    float ampVelSens { 1.0f };

    // Phase 5: matrix slots
    vectron::ModMatrix::Slot slots[vectron::ModMatrix::kNumSlots];
```

Header changes in `VectronVoice.h`:
- Add includes `#include "mod/AdsrEnvelope.h"`, `#include "mod/ModLfo.h"`, `#include "mod/ModMatrix.h"`.
- Replace `juce::ADSR ampAdsr; juce::ADSR filtAdsr;` members with `AdsrEnvelope` and add `AdsrEnvelope modAdsr;`.
- Change setters:

```cpp
    void setAmpAdsr  (const AdsrEnvelope::Parameters& p) { ampAdsr.setParameters (p); }
    void setFiltAdsr (const AdsrEnvelope::Parameters& p) { filtAdsr.setParameters (p); }
    void setModAdsr  (const AdsrEnvelope::Parameters& p) { modAdsr.setParameters (p); }
```

- Override MIDI hooks and add state:

```cpp
    void controllerMoved (int controllerNumber, int newControllerValue) override
    {
        if (controllerNumber == 1)
            modWheel = (float) newControllerValue / 127.0f;
    }
    void channelPressureChanged (int newChannelPressureValue) override
    {
        aftertouch = (float) newChannelPressureValue / 127.0f;
    }
```

- New private members:

```cpp
    ModLfo modLfo[2];
    float  modWheel = 0.0f, modWheelSm = 0.0f;      // raw target + ~5 ms smoothed
    float  aftertouch = 0.0f, aftertouchSm = 0.0f;
    float  ccSmoothCoef = 0.0f;
    float  randNote = 0.0f;                          // per-note random, [-1, 1]
    uint32_t noteRng = 0x9E3779B9u;
    float  prevLfoRateMod[2] { 0.0f, 0.0f };         // 1-sample feedback for LFO-rate dests
```

- [ ] **Step 2: `VectronVoice.cpp` — prepare/applyParams/startNote/stopNote.**

In `prepare` add:

```cpp
    modLfo[0].setSampleRate (sampleRate);
    modLfo[1].setSampleRate (sampleRate);
    modLfo[0].setSeed (1u);
    modLfo[1].setSeed (2u);                          // same seeds in every voice: Global mode lines up
    modAdsr.setSampleRate (sampleRate);
    ccSmoothCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));   // ~5 ms
```

(`ampAdsr.setSampleRate` / `filtAdsr.setSampleRate` calls stay — same method names on `AdsrEnvelope`.) Add `#include <cmath>` if not present.

In `applyParams` add:

```cpp
    for (int n = 0; n < 2; ++n)
    {
        modLfo[n].setShape (static_cast<ModLfo::Shape> (params.modLfoShape[n]));
        modLfo[n].setPolarity (static_cast<ModLfo::Polarity> (params.modLfoPolarity[n]));
        modLfo[n].setPhaseOffsetDegrees (params.modLfoPhaseDeg[n]);
        modLfo[n].setFadeInSeconds (params.modLfoFadeIn[n]);
        modLfo[n].setRate (params.modLfoRateHz[n]);
    }
```

In `startNote` add (after `filtAdsr.noteOn();`):

```cpp
    modAdsr.reset();
    modAdsr.noteOn();
    for (int n = 0; n < 2; ++n)
    {
        if (params.modLfoGlobal[n]) modLfo[n].startFadeIn();   // phase stays global
        else                        modLfo[n].retrigger();
    }
    noteRng = noteRng * 1664525u + 1013904223u;
    randNote = (float) ((noteRng >> 8) & 0xFFFFFFu) / 16777215.0f * 2.0f - 1.0f;
```

In `stopNote`: add `modAdsr.noteOff();` to the tail-off path and `modAdsr.reset();` to the hard-stop path.

- [ ] **Step 3: Rewrite `renderNextBlock`** — full replacement:

```cpp
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

        // 2. LFOs — rate cross-mod uses the previous sample's matrix output
        modLfo[0].setRate (params.modLfoRateHz[0] * std::exp2 (prevLfoRateMod[0] * 3.0f));
        modLfo[1].setRate (params.modLfoRateHz[1] * std::exp2 (prevLfoRateMod[1] * 3.0f));
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
```

- [ ] **Step 4: Processor push (`PluginProcessor.cpp`, `processBlock`).**
Replace the two `juce::ADSR::Parameters` blocks with `AdsrEnvelope::Parameters` (same four loads each) and add a mod-env one:

```cpp
    const AdsrEnvelope::Parameters ampParams {
        pAmpAttack->load(), pAmpDecay->load(), pAmpSustain->load(), pAmpRelease->load() };
    const AdsrEnvelope::Parameters filtParams {
        pFiltAttack->load(), pFiltDecay->load(), pFiltSustain->load(), pFiltRelease->load() };
    const AdsrEnvelope::Parameters modParams {
        pModAttack->load(), pModDecay->load(), pModSustain->load(), pModRelease->load() };
```

After the Phase 4 `vp.*` assignments, add:

```cpp
    // Phase 5: tempo, mod LFOs, matrix
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm())
                bpm = *hostBpm > 0.0 ? *hostBpm : 120.0;

    static constexpr double divBeats[16] = { 4.0, 3.0, 2.0, 4.0/3.0, 1.5, 1.0, 2.0/3.0,
                                             0.75, 0.5, 1.0/3.0, 0.375, 0.25, 1.0/6.0,
                                             0.1875, 0.125, 1.0/12.0 };
    const double sr = getSampleRate();
    for (int n = 0; n < 2; ++n)
    {
        const bool sync = pModLfoSync[n]->load() > 0.5f;
        const int  div  = juce::jlimit (0, 15, (int) pModLfoSyncDiv[n]->load());
        const float hz  = sync ? (float) (bpm / (60.0 * divBeats[div]))
                               : pModLfoRate[n]->load();
        vp.modLfoShape[n]       = (int) pModLfoShape[n]->load();
        vp.modLfoRateHz[n]      = hz;
        vp.modLfoPhaseDeg[n]    = pModLfoPhase[n]->load();
        vp.modLfoFadeIn[n]      = pModLfoFadeIn[n]->load();
        vp.modLfoPolarity[n]    = (int) pModLfoPolarity[n]->load();
        vp.modLfoGlobal[n]      = pModLfoMode[n]->load() > 0.5f;
        vp.modLfoMasterPhase[n] = masterLfoPhase[n];
        if (sr > 0.0)
            masterLfoPhase[n] += (double) buffer.getNumSamples() * (double) hz / sr;
    }

    vp.modVelAmt  = pModVelAmt->load();
    vp.ampVelSens = pAmpVelSens->load();
    for (int s = 0; s < 8; ++s)
    {
        vp.slots[s].source  = (int) pModSrc[s]->load();
        vp.slots[s].dest    = (int) pModDst[s]->load();
        vp.slots[s].amount  = pModAmt[s]->load();
        vp.slots[s].enabled = pModEn[s]->load() > 0.5f;
    }
```

In the voice loop add `v->setModAdsr (modParams);` next to the other two setters.

- [ ] **Step 5: Build everything** — `cmake --build build` (plugin + tests) → green; run full test suite → pass.
- [ ] **Step 6: Commit** — `git commit -m "feat: wire mod LFOs, mod env, exponential envelopes and 8-slot matrix into voice and processor"`

---

### Task 7: Verification

- [ ] **Step 1:** Full clean-ish rebuild: `cmake --build build 2>&1 | tail -5` — zero warnings introduced.
- [ ] **Step 2:** `ctest --test-dir build --output-on-failure` — all tests pass.
- [ ] **Step 3:** Acceptance smoke (PRD §12.5) — Standalone or scripted render: enable slot 1 `LFO1 → Vector X` amount 1.0 (timbre wobbles) and slot 2 `Mod Env → Filter Cutoff` amount 1.0 (sweep per note). If no host at hand, add a temporary offline render check or verify via pluginval: `pluginval --strictness-level 10 <vst3>` if installed.
- [ ] **Step 4:** Request code review (superpowers:requesting-code-review), fix findings.
- [ ] **Step 5:** Merge `feat/phase-5-modulation` → `main`, push.
