# Vectron Phase 2 — Vector Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn Vectron's single-oscillator voice into a 4-oscillator XY vector engine whose timbre morphs as `vector_x`/`vector_y` move, driven by bilinear corner blending plus dedicated per-axis vector LFOs.

**Architecture:** Extend the JUCE-free `PolyBlepOscillator` with Triangle (PolyBLAMP) and Pulse (dual-PolyBLEP + PWM) waves. Add a JUCE-free `VectorEngine` that owns 4 oscillators (corners A/B/C/D), computes bilinear weights from an (x,y) position, and sums `vecOut = gA·A + gB·B + gC·C + gD·D`. Add a JUCE-free `VectorLfo` (5 shapes, free-running) per axis. Wire everything into `VectronVoice`, with the processor pushing all params per block (control rate) and the voice smoothing the base position and adding LFO modulation per sample.

**Tech Stack:** C++20, JUCE 8.0.13 (voice/processor/params only), Catch2 v3.7.1 (pure-C++ DSP tests), CMake ≥ 3.22.

## Global Constraints

- **Framework:** JUCE 8.0.13. **Language:** C++20. **Build:** CMake ≥ 3.22, no Projucer.
- **Names:** plugin `Vectron`; classes `VectronVoice`, `VectronSound`, `VectronProcessor`.
- **Real-time safety:** NO allocations or locks in `processBlock` or voice render. All per-voice state pre-allocated. `juce::ScopedNoDenormals` in `processBlock`.
- **JUCE-free DSP:** `PolyBlepOscillator`, `VectorEngine`, `VectorLfo` must NOT include any JUCE headers — they are unit-tested by `VectronTests`, which links only Catch2 and `#include`s from `source/`. Use `<cmath>`, `<algorithm>`, `<cstdint>` only.
- **Smoothing:** `SmoothedValue<float>` on audible continuous params (base vector position, master volume). LFOs are inherently smooth; no extra smoothing needed on their output.
- **Param IDs:** `snake_case`, exactly as in PRD §8. All `float` unless marked choice/int/bool.
- **Anti-aliasing:** Saw + Pulse via PolyBLEP; Triangle via PolyBLAMP.
- **Acceptance criterion (PRD §12.2):** moving `vector_x`/`vector_y` in the generic editor audibly changes timbre.

## File Structure

- `source/dsp/osc/PolyBlepOscillator.h` — MODIFY: add `Triangle`, `Pulse` to `Wave`; add `setPulseWidth`; add `polyBlamp` helper.
- `source/dsp/osc/VectorEngine.h` — CREATE: engine interface (JUCE-free).
- `source/dsp/osc/VectorEngine.cpp` — CREATE: engine implementation (JUCE-free).
- `source/dsp/osc/VectorLfo.h` — CREATE: header-only per-axis LFO (JUCE-free). *(Addition beyond the master-plan file list; the dedicated vector LFOs in Phase 2's param list need a home, and a small focused class keeps it unit-testable. Phase 5's general `mod/Lfo` is separate.)*
- `source/params/ParameterLayout.cpp` — MODIFY: add all Phase 2 params.
- `source/dsp/VectronVoice.h` / `.cpp` — MODIFY: replace single osc with `VectorEngine` + two `VectorLfo` + smoothed base position; accept a per-block param struct.
- `source/PluginProcessor.cpp` — MODIFY: read Phase 2 params and push to voices per block.
- `CMakeLists.txt` — MODIFY: add `VectorEngine.cpp` to the plugin target.
- `tests/CMakeLists.txt` — MODIFY: add `test_vector_engine.cpp`, `test_vector_lfo.cpp`, and `VectorEngine.cpp` to `VectronTests`.
- `tests/test_oscillator.cpp` — MODIFY: add Triangle + Pulse tests.
- `tests/test_vector_engine.cpp` — CREATE.
- `tests/test_vector_lfo.cpp` — CREATE.

**Corner mapping (PRD §5.2), used everywhere — index 0..3 = A,B,C,D:**
`u=(x+1)/2`, `v=(y+1)/2`; `gA=(1-u)·v` (top-left), `gB=u·v` (top-right), `gC=(1-u)·(1-v)` (bottom-left), `gD=u·(1-v)` (bottom-right).

---

## Task 1: Oscillator — Pulse wave + PWM (PolyBLEP)

**Files:**
- Modify: `source/dsp/osc/PolyBlepOscillator.h`
- Test: `tests/test_oscillator.cpp`

**Interfaces:**
- Consumes: existing `PolyBlepOscillator` (`Wave{Sine,Saw}`, `setSampleRate`, `setFrequency`, `setWave`, `reset`, `processSample`, private `polyBlep`).
- Produces: `enum class Wave { Sine, Triangle, Saw, Pulse }` (order is load-bearing — matches the `oscX_wave` choice param). `void setPulseWidth (float pw) noexcept` clamps to `[0.05, 0.95]`. `Wave::Pulse` renders a band-limited pulse at the current pulse width.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_oscillator.cpp`:

```cpp
#include <cmath>

TEST_CASE ("pulse oscillator stays bounded")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Pulse);
    osc.setPulseWidth (0.5f);
    osc.setFrequency (220.0f);
    osc.reset (0.0f);

    for (int i = 0; i < 96000; ++i)
    {
        const float s = osc.processSample();
        REQUIRE (s >= -1.1f);
        REQUIRE (s <=  1.1f);
    }
}

TEST_CASE ("pulse duty cycle sets the DC mean")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Pulse);
    osc.setFrequency (100.0f);
    osc.reset (0.0f);

    auto meanFor = [&osc] (float pw)
    {
        osc.setPulseWidth (pw);
        osc.reset (0.0f);
        double sum = 0.0;
        const int n = 48000;
        for (int i = 0; i < n; ++i) sum += osc.processSample();
        return (float) (sum / n);
    };

    REQUIRE (std::abs (meanFor (0.5f) - 0.0f)  < 0.02f);   // 2*0.5-1 = 0
    REQUIRE (std::abs (meanFor (0.25f) + 0.5f) < 0.03f);   // 2*0.25-1 = -0.5
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure`
Expected: FAIL — `Wave::Pulse` / `setPulseWidth` do not exist (compile error).

- [ ] **Step 3: Implement Pulse + PWM**

In `source/dsp/osc/PolyBlepOscillator.h`: add `#include <algorithm>` near the top. Change the enum to `enum class Wave { Sine, Triangle, Saw, Pulse };`. Add a setter (public): `void setPulseWidth (float pw) noexcept { pulseWidth = std::clamp (pw, 0.05f, 0.95f); }`. Add the private member `float pulseWidth = 0.5f;`. In `processSample`, add this case to the switch (before the `phase += increment;` line):

```cpp
            case Wave::Pulse:
            {
                value  = (t < pulseWidth) ? 1.0f : -1.0f;
                value += polyBlep (t, increment);            // rising edge at phase 0 (+2 step)
                float t2 = t - pulseWidth;                   // phase relative to the falling edge
                if (t2 < 0.0f) t2 += 1.0f;
                value -= polyBlep (t2, increment);           // falling edge at phase = pw (-2 step)
                break;
            }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure`
Expected: PASS (all oscillator tests).

- [ ] **Step 5: Commit**

```bash
git add source/dsp/osc/PolyBlepOscillator.h tests/test_oscillator.cpp
git commit -m "feat: add band-limited pulse wave with PWM to PolyBlepOscillator"
```

---

## Task 2: Oscillator — Triangle wave (PolyBLAMP)

**Files:**
- Modify: `source/dsp/osc/PolyBlepOscillator.h`
- Test: `tests/test_oscillator.cpp`

**Interfaces:**
- Consumes: `PolyBlepOscillator` with `Wave{Sine,Triangle,Saw,Pulse}` from Task 1.
- Produces: `Wave::Triangle` renders a band-limited triangle: zero at phase 0 rising, peak `+1` at phase 0.25, trough `-1` at phase 0.75, with PolyBLAMP slope-corner correction. Adds a private static `polyBlamp(float t, float dt)` (the integral of `polyBlep`).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_oscillator.cpp`:

```cpp
TEST_CASE ("triangle oscillator runs at the requested frequency")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Triangle);
    osc.setFrequency (440.0f);
    osc.reset (0.0f);

    int risingCrossings = 0;
    float prev = osc.processSample();
    for (int i = 1; i < 48000; ++i)
    {
        const float s = osc.processSample();
        if (prev < 0.0f && s >= 0.0f) ++risingCrossings;
        prev = s;
    }
    REQUIRE (risingCrossings >= 439);
    REQUIRE (risingCrossings <= 441);
}

TEST_CASE ("triangle reaches its peaks and stays bounded")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Triangle);
    osc.setFrequency (50.0f);
    osc.reset (0.0f);

    float maxV = -2.0f, minV = 2.0f;
    for (int i = 0; i < 4800; ++i)   // ~5 cycles
    {
        const float s = osc.processSample();
        maxV = std::max (maxV, s);
        minV = std::min (minV, s);
        REQUIRE (s >= -1.05f);
        REQUIRE (s <=  1.05f);
    }
    REQUIRE (maxV > 0.97f);
    REQUIRE (minV < -0.97f);
}

TEST_CASE ("triangle rising ramp is linear (constant slope)")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Triangle);
    osc.setFrequency (20.0f);        // low freq: dt tiny, corner correction negligible
    osc.reset (0.0f);

    // Collect the first ~quarter cycle (phase 0 -> 0.25), away from corners.
    // At 20 Hz / 48 kHz one cycle = 2400 samples; quarter = 600. Sample 50..550.
    float prev = osc.processSample();
    float firstSlope = 0.0f;
    for (int i = 1; i <= 550; ++i)
    {
        const float s = osc.processSample();
        const float slope = s - prev;
        if (i == 50) firstSlope = slope;
        if (i >= 50 && i <= 550)
            REQUIRE (std::abs (slope - firstSlope) < 1.0e-3f);  // linear, not curved like a sine
        prev = s;
    }
    REQUIRE (firstSlope > 0.0f);     // rising
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure`
Expected: FAIL — triangle currently unhandled (returns 0; crossings/peaks assertions fail).

- [ ] **Step 3: Implement Triangle via PolyBLAMP**

In `source/dsp/osc/PolyBlepOscillator.h`, add this private static helper next to `polyBlep`:

```cpp
    // Integral of polyBlep — corrects slope (1st-derivative) discontinuities.
    static float polyBlamp (float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt)            { const float a = t / dt;        return dt * (a * a - a * a * a / 3.0f - a); }
        if (t > 1.0f - dt)     { const float b = (t - 1.0f) / dt; return dt * ((b + 1.0f) * (b + 1.0f) * (b + 1.0f) / 3.0f); }
        return 0.0f;
    }
```

Add the `Triangle` case to the `processSample` switch:

```cpp
            case Wave::Triangle:
            {
                if      (t < 0.25f) value = 4.0f * t;          // 0 -> +1, slope +4
                else if (t < 0.75f) value = 2.0f - 4.0f * t;   // +1 -> -1, slope -4
                else                value = 4.0f * t - 4.0f;   // -1 -> 0, slope +4

                float rp = t - 0.25f; if (rp < 0.0f) rp += 1.0f;   // rel. to peak corner
                float rt = t - 0.75f; if (rt < 0.0f) rt += 1.0f;   // rel. to trough corner
                value += -8.0f * polyBlamp (rp, increment);        // peak: slope step +4 -> -4
                value +=  8.0f * polyBlamp (rt, increment);        // trough: slope step -4 -> +4
                break;
            }
```

> Note: the `±8` scale is the signed slope-change (per phase) at each corner; `polyBlamp` carries the `dt` factor from the BLEP integral. The linearity + peak + frequency tests pin correctness; if the ramp test shows a sign error, flip the two correction signs.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add source/dsp/osc/PolyBlepOscillator.h tests/test_oscillator.cpp
git commit -m "feat: add PolyBLAMP band-limited triangle wave"
```

---

## Task 3: VectorEngine — bilinear corner weights (pure math)

**Files:**
- Create: `source/dsp/osc/VectorEngine.h`, `source/dsp/osc/VectorEngine.cpp`
- Create: `tests/test_vector_engine.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (pure math).
- Produces: `class VectorEngine`. Nested `enum class Xfade { Linear, EqualPower };` and `struct Weights { float a, b, c, d; };`. Static `Weights computeWeights (float x, float y, Xfade mode) noexcept` — x,y in [-1,1]; weights sum to 1 in both modes; each plane corner isolates one weight to 1.

- [ ] **Step 1: Create the test file (failing)**

`tests/test_vector_engine.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/osc/VectorEngine.h"

using Xfade = VectorEngine::Xfade;

static float weightSum (VectorEngine::Weights w) { return w.a + w.b + w.c + w.d; }

TEST_CASE ("bilinear weights sum to one in both modes")
{
    for (float x = -1.0f; x <= 1.0f; x += 0.25f)
        for (float y = -1.0f; y <= 1.0f; y += 0.25f)
        {
            REQUIRE (std::abs (weightSum (VectorEngine::computeWeights (x, y, Xfade::Linear))     - 1.0f) < 1.0e-5f);
            REQUIRE (std::abs (weightSum (VectorEngine::computeWeights (x, y, Xfade::EqualPower)) - 1.0f) < 1.0e-5f);
        }
}

TEST_CASE ("each plane corner isolates its oscillator")
{
    const auto a = VectorEngine::computeWeights (-1.0f,  1.0f, Xfade::Linear);  // top-left  = A
    const auto b = VectorEngine::computeWeights ( 1.0f,  1.0f, Xfade::Linear);  // top-right = B
    const auto c = VectorEngine::computeWeights (-1.0f, -1.0f, Xfade::Linear);  // bot-left  = C
    const auto d = VectorEngine::computeWeights ( 1.0f, -1.0f, Xfade::Linear);  // bot-right = D

    REQUIRE (a.a > 0.999f); REQUIRE (a.b < 0.001f); REQUIRE (a.c < 0.001f); REQUIRE (a.d < 0.001f);
    REQUIRE (b.b > 0.999f); REQUIRE (b.a < 0.001f); REQUIRE (b.c < 0.001f); REQUIRE (b.d < 0.001f);
    REQUIRE (c.c > 0.999f); REQUIRE (c.a < 0.001f); REQUIRE (c.b < 0.001f); REQUIRE (c.d < 0.001f);
    REQUIRE (d.d > 0.999f); REQUIRE (d.a < 0.001f); REQUIRE (d.b < 0.001f); REQUIRE (d.c < 0.001f);
}

TEST_CASE ("equal-power keeps the center balanced")
{
    const auto w = VectorEngine::computeWeights (0.0f, 0.0f, Xfade::EqualPower);
    REQUIRE (std::abs (w.a - 0.25f) < 1.0e-5f);
    REQUIRE (std::abs (w.b - 0.25f) < 1.0e-5f);
    REQUIRE (std::abs (w.c - 0.25f) < 1.0e-5f);
    REQUIRE (std::abs (w.d - 0.25f) < 1.0e-5f);
}
```

- [ ] **Step 2: Create the header + minimal impl**

`source/dsp/osc/VectorEngine.h`:

```cpp
#pragma once
#include "PolyBlepOscillator.h"

class VectorEngine
{
public:
    enum class Xfade { Linear, EqualPower };
    struct Weights { float a, b, c, d; };

    static Weights computeWeights (float x, float y, Xfade mode) noexcept;
};
```

`source/dsp/osc/VectorEngine.cpp`:

```cpp
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
```

- [ ] **Step 3: Wire the test target**

In `tests/CMakeLists.txt`, change the `add_executable` line to include the new sources:

```cmake
add_executable(VectronTests
    test_oscillator.cpp
    test_vector_engine.cpp
    ${CMAKE_SOURCE_DIR}/source/dsp/osc/VectorEngine.cpp)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake -S . -B build -DVECTRON_BUILD_TESTS=ON && cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure`
Expected: PASS (reconfigure needed because a source file was added).

- [ ] **Step 5: Commit**

```bash
git add source/dsp/osc/VectorEngine.h source/dsp/osc/VectorEngine.cpp tests/test_vector_engine.cpp tests/CMakeLists.txt
git commit -m "feat: add VectorEngine bilinear corner-weight computation"
```

---

## Task 4: VectorEngine — 4 oscillators, tuning, render

**Files:**
- Modify: `source/dsp/osc/VectorEngine.h`, `source/dsp/osc/VectorEngine.cpp`
- Modify: `tests/test_vector_engine.cpp`

**Interfaces:**
- Consumes: `PolyBlepOscillator` (Tasks 1-2), `computeWeights` (Task 3).
- Produces, added to `VectorEngine`:
  - `void setSampleRate (double sr) noexcept;`
  - `void setWave (int idx, PolyBlepOscillator::Wave w) noexcept;`
  - `void setLevel (int idx, float level) noexcept;`            // 0..1 base gain
  - `void setPulseWidth (int idx, float pw) noexcept;`
  - `void setDetune (int idx, int octave, int coarseSemis, float fineCents) noexcept;`
  - `void setPhaseResetEnabled (int idx, bool enabled) noexcept;`
  - `void setXfadeMode (Xfade mode) noexcept;`
  - `void setNoteFrequency (float baseHz) noexcept;`            // base pitch; each osc = base * 2^(oct + coarse/12 + fine/1200)
  - `void noteOn() noexcept;`                                   // reset phase on oscillators whose phaseReset is enabled
  - `void setVectorPosition (float x, float y) noexcept;`       // recompute weights for this sample
  - `float processSample() noexcept;`                           // advance ALL 4 oscs; return gA·A·levA + gB·B·levB + gC·C·levC + gD·D·levD
  - `idx` is 0..3 = A,B,C,D.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_vector_engine.cpp`:

```cpp
static int countRisingZeroCrossings (VectorEngine& e, int numSamples)
{
    int crossings = 0;
    float prev = e.processSample();
    for (int i = 1; i < numSamples; ++i)
    {
        const float s = e.processSample();
        if (prev < 0.0f && s >= 0.0f) ++crossings;
        prev = s;
    }
    return crossings;
}

TEST_CASE ("a corner position plays only that oscillator's pitch")
{
    VectorEngine e;
    e.setSampleRate (48000.0);
    for (int i = 0; i < 4; ++i) { e.setWave (i, PolyBlepOscillator::Wave::Sine); e.setLevel (i, 1.0f); }
    e.setNoteFrequency (440.0f);
    e.setVectorPosition (-1.0f, 1.0f);   // corner A only
    e.noteOn();

    const int crossings = countRisingZeroCrossings (e, 48000);
    REQUIRE (crossings >= 439);
    REQUIRE (crossings <= 441);
}

TEST_CASE ("octave and coarse detune scale the corner frequency")
{
    VectorEngine e;
    e.setSampleRate (48000.0);
    for (int i = 0; i < 4; ++i) { e.setWave (i, PolyBlepOscillator::Wave::Sine); e.setLevel (i, 1.0f); }
    e.setVectorPosition (1.0f, 1.0f);    // corner B only (top-right)
    e.setNoteFrequency (220.0f);

    SECTION ("octave +1 doubles")
    {
        e.setDetune (1, 1, 0, 0.0f);     // osc B: +1 octave -> 440 Hz
        e.noteOn();
        const int crossings = countRisingZeroCrossings (e, 48000);
        REQUIRE (crossings >= 438);
        REQUIRE (crossings <= 442);
    }
    SECTION ("coarse +12 semitones doubles")
    {
        e.setDetune (1, 0, 12, 0.0f);    // osc B: +12 semis -> 440 Hz
        e.noteOn();
        const int crossings = countRisingZeroCrossings (e, 48000);
        REQUIRE (crossings >= 438);
        REQUIRE (crossings <= 442);
    }
}

TEST_CASE ("levels scale a corner's contribution")
{
    VectorEngine e;
    e.setSampleRate (48000.0);
    for (int i = 0; i < 4; ++i) e.setWave (i, PolyBlepOscillator::Wave::Sine);
    e.setNoteFrequency (440.0f);
    e.setVectorPosition (-1.0f, 1.0f);   // corner A only
    e.setLevel (0, 0.0f);                // mute A
    e.noteOn();

    float maxAbs = 0.0f;
    for (int i = 0; i < 4800; ++i) maxAbs = std::max (maxAbs, std::abs (e.processSample()));
    REQUIRE (maxAbs < 1.0e-4f);          // silent when the only active corner is muted
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure`
Expected: FAIL — new methods do not exist (compile error).

- [ ] **Step 3: Extend the header**

Replace `source/dsp/osc/VectorEngine.h` with:

```cpp
#pragma once
#include "PolyBlepOscillator.h"

class VectorEngine
{
public:
    enum class Xfade { Linear, EqualPower };
    struct Weights { float a, b, c, d; };

    static Weights computeWeights (float x, float y, Xfade mode) noexcept;

    void setSampleRate (double sr) noexcept;
    void setWave (int idx, PolyBlepOscillator::Wave w) noexcept;
    void setLevel (int idx, float level) noexcept;
    void setPulseWidth (int idx, float pw) noexcept;
    void setDetune (int idx, int octave, int coarseSemis, float fineCents) noexcept;
    void setPhaseResetEnabled (int idx, bool enabled) noexcept;
    void setXfadeMode (Xfade mode) noexcept;
    void setNoteFrequency (float baseHz) noexcept;
    void noteOn() noexcept;
    void setVectorPosition (float x, float y) noexcept;
    float processSample() noexcept;

private:
    void updateFrequency (int idx) noexcept;

    static constexpr int kNumOsc = 4;
    PolyBlepOscillator osc[kNumOsc];
    float level[kNumOsc]      { 1.0f, 1.0f, 1.0f, 1.0f };
    int   octave[kNumOsc]     { 0, 0, 0, 0 };
    int   coarse[kNumOsc]     { 0, 0, 0, 0 };
    float fine[kNumOsc]       { 0.0f, 0.0f, 0.0f, 0.0f };
    bool  phaseReset[kNumOsc] { true, true, true, true };
    float baseHz   = 440.0f;
    Xfade xfade    = Xfade::Linear;
    Weights weight { 0.25f, 0.25f, 0.25f, 0.25f };
};
```

- [ ] **Step 4: Extend the implementation**

Append to `source/dsp/osc/VectorEngine.cpp` (keep `computeWeights` from Task 3; add `#include <cmath>` already present):

```cpp
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
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add source/dsp/osc/VectorEngine.h source/dsp/osc/VectorEngine.cpp tests/test_vector_engine.cpp
git commit -m "feat: VectorEngine 4-oscillator render with per-corner tuning and levels"
```

---

## Task 5: VectorLfo — free-running per-axis LFO (5 shapes)

**Files:**
- Create: `source/dsp/osc/VectorLfo.h` (header-only, JUCE-free)
- Create: `tests/test_vector_lfo.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `class VectorLfo`. Nested `enum class Shape { Sine, Triangle, Saw, Square, SampleHold };`. Methods: `setSampleRate(double)`, `setRate(float hz)`, `setDepth(float 0..1)`, `setShape(Shape)`, `reset()` (phase to 0, seed a fresh S&H value), `float processSample()` returning the shape value scaled by depth, in `[-depth, +depth]`, advancing phase. S&H resamples a new pseudo-random value on each phase wrap (deterministic LCG — RT-safe, no allocation).

- [ ] **Step 1: Create the test file (failing)**

`tests/test_vector_lfo.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/osc/VectorLfo.h"

TEST_CASE ("sine LFO runs at the requested rate")
{
    VectorLfo lfo;
    lfo.setSampleRate (48000.0);
    lfo.setShape (VectorLfo::Shape::Sine);
    lfo.setRate (5.0f);
    lfo.setDepth (1.0f);
    lfo.reset();

    int risingCrossings = 0;
    float prev = lfo.processSample();
    for (int i = 1; i < 48000; ++i)
    {
        const float s = lfo.processSample();
        if (prev < 0.0f && s >= 0.0f) ++risingCrossings;
        prev = s;
    }
    REQUIRE (risingCrossings >= 4);
    REQUIRE (risingCrossings <= 6);
}

TEST_CASE ("depth scales LFO amplitude and bounds output")
{
    VectorLfo lfo;
    lfo.setSampleRate (48000.0);
    lfo.setShape (VectorLfo::Shape::Sine);
    lfo.setRate (2.0f);
    lfo.setDepth (0.5f);
    lfo.reset();

    float maxAbs = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const float s = lfo.processSample();
        maxAbs = std::max (maxAbs, std::abs (s));
        REQUIRE (s >= -0.5001f);
        REQUIRE (s <=  0.5001f);
    }
    REQUIRE (maxAbs > 0.49f);
}

TEST_CASE ("square LFO only emits plus/minus depth")
{
    VectorLfo lfo;
    lfo.setSampleRate (48000.0);
    lfo.setShape (VectorLfo::Shape::Square);
    lfo.setRate (3.0f);
    lfo.setDepth (0.8f);
    lfo.reset();

    for (int i = 0; i < 48000; ++i)
    {
        const float s = lfo.processSample();
        REQUIRE ((std::abs (s - 0.8f) < 1.0e-5f || std::abs (s + 0.8f) < 1.0e-5f));
    }
}

TEST_CASE ("sample-and-hold holds value between rate ticks")
{
    VectorLfo lfo;
    lfo.setSampleRate (48000.0);
    lfo.setShape (VectorLfo::Shape::SampleHold);
    lfo.setRate (10.0f);          // one step = 4800 samples
    lfo.setDepth (1.0f);
    lfo.reset();

    float first = lfo.processSample();
    int changes = 0;
    float prev = first;
    for (int i = 1; i < 4800; ++i)   // within the first step: must hold
    {
        const float s = lfo.processSample();
        if (std::abs (s - prev) > 1.0e-6f) ++changes;
        REQUIRE (s >= -1.0001f);
        REQUIRE (s <=  1.0001f);
        prev = s;
    }
    REQUIRE (changes == 0);          // constant within a step

    // Over 2 seconds (~20 steps) the value must change at least a few times.
    int stepChanges = 0;
    float held = prev;
    for (int i = 0; i < 96000; ++i)
    {
        const float s = lfo.processSample();
        if (std::abs (s - held) > 1.0e-6f) { ++stepChanges; held = s; }
    }
    REQUIRE (stepChanges >= 5);
}
```

- [ ] **Step 2: Create the header**

`source/dsp/osc/VectorLfo.h`:

```cpp
#pragma once
#include <cmath>
#include <cstdint>

class VectorLfo
{
public:
    enum class Shape { Sine, Triangle, Saw, Square, SampleHold };

    void setSampleRate (double sr) noexcept { sampleRate = sr; updateIncrement(); }
    void setRate (float hz)        noexcept { rate = hz; updateIncrement(); }
    void setDepth (float d)        noexcept { depth = d; }
    void setShape (Shape s)        noexcept { shape = s; }

    void reset() noexcept
    {
        phase = 0.0f;
        shValue = nextRandom();
    }

    float processSample() noexcept
    {
        float v = 0.0f;
        switch (shape)
        {
            case Shape::Sine:       v = std::sin (kTwoPi * phase); break;
            case Shape::Triangle:   v = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase); break;
            case Shape::Saw:        v = 2.0f * phase - 1.0f; break;
            case Shape::Square:     v = (phase < 0.5f) ? 1.0f : -1.0f; break;
            case Shape::SampleHold: v = shValue; break;
        }

        phase += increment;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            shValue = nextRandom();   // resample S&H on each wrap
        }
        return v * depth;
    }

private:
    static constexpr float kTwoPi = 6.283185307179586f;

    void updateIncrement() noexcept
    {
        increment = (sampleRate > 0.0) ? static_cast<float> (rate / sampleRate) : 0.0f;
    }

    float nextRandom() noexcept
    {
        rngState = rngState * 1664525u + 1013904223u;            // LCG
        const float unit = static_cast<float> ((rngState >> 8) & 0xFFFFFFu) / 16777215.0f; // [0,1]
        return unit * 2.0f - 1.0f;                               // [-1,1]
    }

    double  sampleRate = 44100.0;
    float   rate       = 1.0f;
    float   depth      = 0.0f;
    float   increment  = 0.0f;
    float   phase      = 0.0f;
    float   shValue    = 0.0f;
    Shape   shape      = Shape::Sine;
    uint32_t rngState  = 22222u;
};
```

- [ ] **Step 3: Wire the test target**

In `tests/CMakeLists.txt`, add `test_vector_lfo.cpp` to the `add_executable(VectronTests ...)` source list:

```cmake
add_executable(VectronTests
    test_oscillator.cpp
    test_vector_engine.cpp
    test_vector_lfo.cpp
    ${CMAKE_SOURCE_DIR}/source/dsp/osc/VectorEngine.cpp)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake -S . -B build -DVECTRON_BUILD_TESTS=ON && cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add source/dsp/osc/VectorLfo.h tests/test_vector_lfo.cpp tests/CMakeLists.txt
git commit -m "feat: add free-running VectorLfo with 5 shapes and S&H"
```

---

## Task 6: Parameter layout — Phase 2 params

**Files:**
- Modify: `source/params/ParameterLayout.cpp`

**Interfaces:**
- Consumes: existing `createParameterLayout()` (amp ADSR, master_volume, master_tune).
- Produces APVTS params (PRD §8). Choice-param option ORDER is load-bearing (indices map to enums):
  - `oscX_wave` (X ∈ {A,B,C,D}): choice `{Sine, Triangle, Saw, Pulse}`.
  - `oscX_oct`: int −3..3 (default 0). `oscX_coarse`: int −24..24 (default 0). `oscX_fine`: float −100..100 cents (default 0). `oscX_pw`: float 0.05..0.95 (default 0.5). `oscX_level`: float 0..1 (default 1). `oscX_phaseReset`: bool (default true).
  - `vector_x`, `vector_y`: float −1..1 (default 0). `vector_xfade`: choice `{Linear, Equal-Power}` (default Linear). `vector_level`: float 0..1 (default 1).
  - `vector_xLfoRate`, `vector_yLfoRate`: float 0.01..20 Hz, log skew (default 1). `vector_xLfoDepth`, `vector_yLfoDepth`: float 0..1 (default 0). `vector_xLfoShape`, `vector_yLfoShape`: choice `{Sine, Triangle, Saw, Square, S&H}` (default Sine).
- Default waves chosen so the four corners differ (satisfies "moving XY changes timbre"): A=Saw, B=Pulse, C=Sine, D=Triangle.

- [ ] **Step 1: Add the params**

Edit `source/params/ParameterLayout.cpp`. Add `using API = juce::AudioParameterInt; using APC = juce::AudioParameterChoice; using APB = juce::AudioParameterBool;` beside the existing `using APF`. Add a log range helper beside `timeRange`:

```cpp
    static juce::NormalisableRange<float> lfoRateRange()
    {
        juce::NormalisableRange<float> r { 0.01f, 20.0f, 0.001f };
        r.setSkewForCentre (1.0f);
        return r;
    }
```

Inside `createParameterLayout()`, before `return layout;`, add the oscillator loop + vector params:

```cpp
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
```

> Param IDs produced: `vector_xLfoRate`, `vector_xLfoDepth`, `vector_xLfoShape`, `vector_yLfoRate`, `vector_yLfoDepth`, `vector_yLfoShape` (matches PRD `vector_{x,y}Lfo{Rate,Depth,Shape}`).

- [ ] **Step 2: Build the plugin to verify params compile + register**

Run: `cmake --build build --target Vectron`
Expected: builds clean (no test for the layout — `VectronTests` does not link JUCE; correctness is verified by the build and the generic-editor host check in Task 7).

- [ ] **Step 3: Commit**

```bash
git add source/params/ParameterLayout.cpp
git commit -m "feat: add oscillator, vector, and vector-LFO parameters"
```

---

## Task 7: Wire VectorEngine + LFOs into the voice and processor

**Files:**
- Modify: `source/dsp/VectronVoice.h`, `source/dsp/VectronVoice.cpp`
- Modify: `source/PluginProcessor.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `VectorEngine` (Tasks 3-4), `VectorLfo` (Task 5), all Phase 2 params (Task 6).
- Produces: `struct VectronVoiceParams` (per-block control-rate snapshot) and `void VectronVoice::setVectorParams (const VectronVoiceParams&) noexcept`. The processor builds one `VectronVoiceParams` per block from APVTS and pushes it to every voice (same pattern as the existing ADSR push). Voice render: per sample, advance both LFOs, smooth the base X/Y, compute `finalX/Y = clamp(base + lfo, -1, 1)`, set the engine position, and scale the engine output by `vector_level · env · velocity · 0.3`.

- [ ] **Step 1: Add VectorEngine.cpp to the plugin target**

In `CMakeLists.txt`, extend `target_sources(Vectron PRIVATE ...)`:

```cmake
target_sources(Vectron PRIVATE
    source/PluginProcessor.cpp
    source/params/ParameterLayout.cpp
    source/dsp/VectronVoice.cpp
    source/dsp/osc/VectorEngine.cpp)
```

- [ ] **Step 2: Rewrite the voice header**

Replace `source/dsp/VectronVoice.h` with:

```cpp
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "osc/VectorEngine.h"
#include "osc/VectorLfo.h"

struct VectronVoiceParams
{
    int   oscWave[4]       { 2, 3, 0, 1 };
    int   oscOct[4]        { 0, 0, 0, 0 };
    int   oscCoarse[4]     { 0, 0, 0, 0 };
    float oscFine[4]       { 0.0f, 0.0f, 0.0f, 0.0f };
    float oscPw[4]         { 0.5f, 0.5f, 0.5f, 0.5f };
    float oscLevel[4]      { 1.0f, 1.0f, 1.0f, 1.0f };
    bool  oscPhaseReset[4] { true, true, true, true };
    int   xfade            { 0 };           // 0 Linear, 1 Equal-Power
    float vectorLevel      { 1.0f };
    float baseX            { 0.0f };
    float baseY            { 0.0f };
    float lfoRate[2]       { 1.0f, 1.0f };  // [0]=X, [1]=Y
    float lfoDepth[2]      { 0.0f, 0.0f };
    int   lfoShape[2]      { 0, 0 };
};

class VectronVoice : public juce::SynthesiserVoice
{
public:
    void prepare (double sampleRate, int blockSize);
    void setAmpAdsr (const juce::ADSR::Parameters& p) { ampAdsr.setParameters (p); }
    void setMasterTune (float a4Hz) { masterTuneHz = a4Hz; }
    void setVectorParams (const VectronVoiceParams& p) noexcept { params = p; applyParams(); }

    bool canPlaySound (juce::SynthesiserSound*) override;
    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override;

private:
    void applyParams() noexcept;

    VectorEngine engine;
    VectorLfo    lfo[2];                 // [0]=X, [1]=Y
    juce::SmoothedValue<float> baseX, baseY;
    juce::ADSR   ampAdsr;
    VectronVoiceParams params;
    float level = 0.0f;
    float masterTuneHz = 440.0f;
};
```

- [ ] **Step 3: Rewrite the voice implementation**

Replace `source/dsp/VectronVoice.cpp` with:

```cpp
#include "VectronVoice.h"
#include "VectronSound.h"

void VectronVoice::prepare (double sampleRate, int /*blockSize*/)
{
    engine.setSampleRate (sampleRate);
    lfo[0].setSampleRate (sampleRate);
    lfo[1].setSampleRate (sampleRate);
    ampAdsr.setSampleRate (sampleRate);
    baseX.reset (sampleRate, 0.01);
    baseY.reset (sampleRate, 0.01);
    applyParams();
}

void VectronVoice::applyParams() noexcept
{
    for (int i = 0; i < 4; ++i)
    {
        engine.setWave (i, static_cast<PolyBlepOscillator::Wave> (params.oscWave[i]));
        engine.setDetune (i, params.oscOct[i], params.oscCoarse[i], params.oscFine[i]);
        engine.setPulseWidth (i, params.oscPw[i]);
        engine.setLevel (i, params.oscLevel[i]);
        engine.setPhaseResetEnabled (i, params.oscPhaseReset[i]);
    }
    engine.setXfadeMode (static_cast<VectorEngine::Xfade> (params.xfade));

    for (int a = 0; a < 2; ++a)
    {
        lfo[a].setRate (params.lfoRate[a]);
        lfo[a].setDepth (params.lfoDepth[a]);
        lfo[a].setShape (static_cast<VectorLfo::Shape> (params.lfoShape[a]));
    }

    baseX.setTargetValue (params.baseX);
    baseY.setTargetValue (params.baseY);
}

bool VectronVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<VectronSound*> (sound) != nullptr;
}

void VectronVoice::startNote (int midiNoteNumber, float velocity,
                              juce::SynthesiserSound*, int)
{
    engine.setNoteFrequency ((float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, masterTuneHz));
    engine.noteOn();
    lfo[0].reset();
    lfo[1].reset();
    baseX.setCurrentAndTargetValue (params.baseX);
    baseY.setCurrentAndTargetValue (params.baseY);
    level = velocity;
    ampAdsr.noteOn();
}

void VectronVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        ampAdsr.noteOff();
    }
    else
    {
        ampAdsr.reset();
        clearCurrentNote();
    }
}

void VectronVoice::renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    if (! ampAdsr.isActive())
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const float lx = lfo[0].processSample();
        const float ly = lfo[1].processSample();
        const float fx = juce::jlimit (-1.0f, 1.0f, baseX.getNextValue() + lx);
        const float fy = juce::jlimit (-1.0f, 1.0f, baseY.getNextValue() + ly);
        engine.setVectorPosition (fx, fy);

        const float env = ampAdsr.getNextSample();
        const float s   = engine.processSample() * params.vectorLevel * env * level * 0.3f;

        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            output.addSample (ch, startSample + i, s);

        if (! ampAdsr.isActive())
        {
            clearCurrentNote();
            return;
        }
    }
}
```

- [ ] **Step 4: Push params from the processor**

In `source/PluginProcessor.cpp` `processBlock`, replace the per-voice push loop (the block that sets ADSR + master tune) with one that also builds and pushes `VectronVoiceParams`. Add a small local helper at the top of `processBlock` after the existing `ampParams`/`tuneHz` reads:

```cpp
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    VectronVoiceParams vp;
    const char* oscIds[4] { "oscA", "oscB", "oscC", "oscD" };
    for (int i = 0; i < 4; ++i)
    {
        const juce::String id { oscIds[i] };
        vp.oscWave[i]       = (int)   raw ((id + "_wave").toRawUTF8());
        vp.oscOct[i]        = (int)   raw ((id + "_oct").toRawUTF8());
        vp.oscCoarse[i]     = (int)   raw ((id + "_coarse").toRawUTF8());
        vp.oscFine[i]       =         raw ((id + "_fine").toRawUTF8());
        vp.oscPw[i]         =         raw ((id + "_pw").toRawUTF8());
        vp.oscLevel[i]      =         raw ((id + "_level").toRawUTF8());
        vp.oscPhaseReset[i] =         raw ((id + "_phaseReset").toRawUTF8()) > 0.5f;
    }
    vp.xfade       = (int) raw ("vector_xfade");
    vp.vectorLevel =       raw ("vector_level");
    vp.baseX       =       raw ("vector_x");
    vp.baseY       =       raw ("vector_y");
    const char* axisId[2] { "vector_x", "vector_y" };
    for (int a = 0; a < 2; ++a)
    {
        const juce::String id { axisId[a] };
        vp.lfoRate[a]  =       raw ((id + "LfoRate").toRawUTF8());
        vp.lfoDepth[a] =       raw ((id + "LfoDepth").toRawUTF8());
        vp.lfoShape[a] = (int) raw ((id + "LfoShape").toRawUTF8());
    }
```

Then in the existing `for (int i = 0; i < synth.getNumVoices(); ++i)` loop that casts to `VectronVoice*`, add `v->setVectorParams (vp);` alongside `v->setAmpAdsr (ampParams);` and `v->setMasterTune (tuneHz);`.

> `getRawParameterValue` returns `std::atomic<float>*`; reading once per block is RT-safe (no allocation). Choice/int/bool params are stored as their numeric index/value, so the casts above are correct.

- [ ] **Step 5: Build everything + run tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: plugin + tests build clean; all DSP tests PASS.

- [ ] **Step 6: Manual host verification (acceptance criterion)**

1. Launch the Standalone: `open build/Vectron_artefacts/Standalone/Vectron.app` (or the Debug/Release subpath your build produced).
2. Play and hold a note (MIDI keyboard or the on-screen keys). You should hear a tone.
3. In the generic editor, sweep `Vector X` from −1 → +1 and `Vector Y` from −1 → +1.
4. **PASS:** the timbre audibly morphs between the four corner waves (Saw / Pulse / Sine / Triangle) as the position moves. Set `Vector X LFO Depth` > 0 and confirm the timbre wobbles on its own.

Document the result (PASS/observations) in the Phase 2 report.

- [ ] **Step 7: Commit**

```bash
git add source/dsp/VectronVoice.h source/dsp/VectronVoice.cpp source/PluginProcessor.cpp CMakeLists.txt
git commit -m "feat: wire VectorEngine + vector LFOs into the voice and processor"
```

---

## Verification (Phase 2 end-to-end)

- [x] `cmake --build build && ctest --test-dir build --output-on-failure` — all oscillator, vector-engine, and vector-LFO tests pass. **✅ 18/18 pass (2026-07-22).**
- [x] Plugin builds VST3 + Standalone with no new warnings. **✅ `Vectron.exe` + `Vectron.vst3` build clean. Note: the VST3 post-build auto-install to `C:\Program Files\Common Files\VST3` fails without admin — the `.vst3` bundle itself builds fine at `build/Vectron_artefacts/Debug/VST3/`.**
- [ ] Manual host check (Task 7 Step 6): moving `vector_x`/`vector_y` audibly changes timbre; vector LFO depth animates it. **⏳ PENDING USER — requires listening; launch `build/Vectron_artefacts/Debug/Standalone/Vectron.exe`.**
- [x] `pluginval --strictness-level 10 --validate-in-process <path-to-Vectron.vst3>` passes. **✅ PASSED strictness 10 (2026-07-22) — all sections completed, 0 failures, "SUCCESS". pluginval downloaded to scratchpad (not on machine).**

## Notes / Decisions Locked (Phase 2)

- **Vector LFOs:** built now, free-running, per-voice (retriggered at note-on). Tempo-sync deferred to Phase 5.
- **Triangle:** PolyBLAMP band-limited (not naive).
- **Final position model:** `final = clamp(smoothedBase + vectorLFO, -1, 1)`. The trajectory (`traj_*`) and mod-matrix (`mods_*`) terms from PRD §5.2 are added in Phases 6 and 5 respectively.
- **Smoothing:** base X/Y via `juce::SmoothedValue` (10 ms) in the voice; LFO output added per sample (no extra smoothing).
- **DSP stays JUCE-free:** `PolyBlepOscillator`, `VectorEngine`, `VectorLfo` — unit-tested without JUCE.
