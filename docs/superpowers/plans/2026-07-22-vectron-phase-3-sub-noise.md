# Vectron Phase 3 — Sub + Noise Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a sub oscillator (body/weight) and a first-class variable noise generator (white→pink→brown color morph, pitch-tracked tuned band-pass, an always-on HP/BP/LP filter, and a built-but-unrouted Sample & Hold value) to the voice mixer, so a patch can go from clean analog tone to rich texture.

**Architecture:** Three new JUCE-free, unit-tested leaf classes — `SvfFilter` (TPT/Zavalishin state-variable filter, pulled forward from Phase 4 and reused there), `SubOscillator` (wraps the existing `PolyBlepOscillator`), and `NoiseGenerator` (color morph + two `SvfFilter`s + S&H). All three are summed into the per-voice mixer alongside the vector engine, exactly where Phase 4's filter/drive will later insert. The processor reads the new APVTS params per block via cached atomic pointers (existing pattern).

**Tech Stack:** C++20, JUCE 8.0.13 (voice/processor/params only), Catch2 v3.7.1 (pure-C++ DSP tests), CMake ≥ 3.22.

## Global Constraints

- **Framework:** JUCE 8.0.13. **Language:** C++20. **Build:** CMake ≥ 3.22, no Projucer.
- **Names:** plugin `Vectron`; classes `VectronVoice`, `VectronSound`, `VectronProcessor`.
- **Real-time safety:** NO allocations or locks in `processBlock` or voice render. All per-voice state pre-allocated. Noise RNG is a deterministic LCG (RT-safe), same style as `VectorLfo`.
- **JUCE-free DSP:** `SvfFilter`, `SubOscillator`, `NoiseGenerator` must NOT include any JUCE header — they are unit-tested by `VectronTests`, which links only Catch2 and `#include`s from `source/`. Use `<cmath>`, `<algorithm>`, `<cstdint>` only.
- **Smoothing:** `juce::SmoothedValue<float>` on audible continuous levels (sub level), matching the existing `vectorLevel` smoothing in the voice.
- **Param IDs:** `snake_case`, exactly as in PRD §8. Choice-param option ORDER is load-bearing (indices map to enums).
- **Acceptance criterion (PRD §12.3):** audible variable noise (color sweep + tuned mode) and audible sub weight.
- **Design source of truth:** `docs/superpowers/specs/2026-07-22-vectron-phase-3-sub-noise-design.md`.

### Build & test commands (this machine — Windows / MSVC)

`cmake` and `ctest` are NOT on PATH. Use the full paths (PowerShell). The build config is `Debug`; tests must be configured ON.

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

# Reconfigure (needed whenever a CMakeLists changes / a source file is added):
& $cmake -S . -B build -DVECTRON_BUILD_TESTS=ON

# Build + run tests:
& $cmake --build build --config Debug --target VectronTests
& $ctest --test-dir build -C Debug --output-on-failure

# Build the plugin (Standalone avoids the admin-only VST3 system-dir copy):
& $cmake --build build --config Debug --target Vectron_Standalone
```

Below, run steps are written generically as `cmake --build ...` / `ctest ...`; on this machine substitute the full paths above.

## File Structure

- `source/dsp/filter/SvfFilter.h` — CREATE. Header-only TPT SVF (LP/BP/HP/Notch). JUCE-free. Reused as the main filter in Phase 4.
- `source/dsp/osc/SubOscillator.h` — CREATE. Header-only. Wraps `PolyBlepOscillator`; sine/tri/square, −1/−2 oct, pitch tracking.
- `source/dsp/noise/NoiseGenerator.h` / `.cpp` — CREATE. Color morph + tuned BP + noise filter + S&H. JUCE-free.
- `source/params/ParameterLayout.cpp` — MODIFY. Add `sub_*` + `noise_*` params.
- `source/dsp/VectronVoice.h` / `.cpp` — MODIFY. Add `SubOscillator` + `NoiseGenerator`; extend `VectronVoiceParams`; sum into the mixer.
- `source/PluginProcessor.h` / `.cpp` — MODIFY. Cache + read the new params, push per block.
- `CMakeLists.txt` — MODIFY. Add `NoiseGenerator.cpp` to the `Vectron` target.
- `tests/CMakeLists.txt` — MODIFY. Add the three test files + `NoiseGenerator.cpp` to `VectronTests`.
- `tests/test_svf_filter.cpp`, `tests/test_sub_oscillator.cpp`, `tests/test_noise_generator.cpp` — CREATE.

---

## Task 1: SvfFilter — TPT state-variable filter (pure DSP)

**Files:**
- Create: `source/dsp/filter/SvfFilter.h`
- Create: `tests/test_svf_filter.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (pure math).
- Produces: `class SvfFilter`. Nested `enum class Mode { LP, BP, HP, Notch };`. Methods: `setSampleRate(double)`, `setCutoff(float hz)`, `setResonance(float 0..1)`, `setMode(Mode)`, `reset()`, `float processSample(float x)`. Resonance 0 → gentle (Q≈0.5); resonance 1 → high-Q (narrow). Cutoff internally clamped to `[20, 0.99·Nyquist]`. Unconditionally stable.

- [ ] **Step 1: Create the failing test file**

`tests/test_svf_filter.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include "dsp/filter/SvfFilter.h"

// Steady-state RMS gain of the filter at a given sine frequency.
static float rmsGainAt (SvfFilter& f, float freqHz, double sr)
{
    f.reset();
    const int n    = (int) sr;       // 1 second
    const int skip = n / 4;          // discard transient
    double num = 0.0, den = 0.0;
    const float w = 2.0f * 3.14159265358979f * freqHz / (float) sr;
    for (int i = 0; i < n; ++i)
    {
        const float x = std::sin (w * i);
        const float y = f.processSample (x);
        if (i >= skip) { num += (double) y * y; den += (double) x * x; }
    }
    return (float) std::sqrt (num / den);
}

TEST_CASE ("SVF lowpass passes lows and cuts highs")
{
    SvfFilter f;
    f.setSampleRate (48000.0);
    f.setMode (SvfFilter::Mode::LP);
    f.setResonance (0.0f);
    f.setCutoff (1000.0f);

    REQUIRE (rmsGainAt (f, 100.0f,   48000.0) > 0.7f);
    REQUIRE (rmsGainAt (f, 12000.0f, 48000.0) < 0.2f);
}

TEST_CASE ("SVF highpass cuts lows and passes highs")
{
    SvfFilter f;
    f.setSampleRate (48000.0);
    f.setMode (SvfFilter::Mode::HP);
    f.setResonance (0.0f);
    f.setCutoff (1000.0f);

    REQUIRE (rmsGainAt (f, 100.0f,  48000.0) < 0.2f);
    REQUIRE (rmsGainAt (f, 8000.0f, 48000.0) > 0.7f);
}

TEST_CASE ("SVF bandpass peaks near cutoff")
{
    SvfFilter f;
    f.setSampleRate (48000.0);
    f.setMode (SvfFilter::Mode::BP);
    f.setResonance (0.5f);
    f.setCutoff (1000.0f);

    const float atCenter = rmsGainAt (f, 1000.0f,  48000.0);
    const float belowC   = rmsGainAt (f, 100.0f,   48000.0);
    const float aboveC   = rmsGainAt (f, 10000.0f, 48000.0);
    REQUIRE (atCenter > belowC);
    REQUIRE (atCenter > aboveC);
}

TEST_CASE ("SVF stays bounded under fast cutoff modulation")
{
    SvfFilter f;
    f.setSampleRate (48000.0);
    f.setMode (SvfFilter::Mode::LP);
    f.setResonance (0.9f);
    f.reset();

    uint32_t rng = 1u;
    for (int i = 0; i < 96000; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        const float x = (float) ((rng >> 8) & 0xFFFFFFu) / 16777216.0f * 2.0f - 1.0f; // [-1,1)
        const float cutoff = 100.0f + 15000.0f * (0.5f + 0.5f * std::sin (0.01f * i));
        f.setCutoff (cutoff);
        const float y = f.processSample (x);
        REQUIRE (std::isfinite (y));
        REQUIRE (std::abs (y) < 100.0f);
    }
}
```

- [ ] **Step 2: Wire the test target**

In `tests/CMakeLists.txt`, add `test_svf_filter.cpp` to the `add_executable(VectronTests ...)` source list:

```cmake
add_executable(VectronTests
    test_oscillator.cpp
    test_vector_engine.cpp
    test_vector_lfo.cpp
    test_svf_filter.cpp
    ${CMAKE_SOURCE_DIR}/source/dsp/osc/VectorEngine.cpp)
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake -S . -B build -DVECTRON_BUILD_TESTS=ON && cmake --build build --config Debug --target VectronTests`
Expected: FAIL — `dsp/filter/SvfFilter.h` not found (compile error).

- [ ] **Step 4: Create the header**

`source/dsp/filter/SvfFilter.h`:

```cpp
#pragma once
#include <cmath>
#include <algorithm>

// TPT / Zavalishin state-variable filter (Andrew Simper topology).
// One input -> LP/BP/HP/Notch. Unconditionally stable. JUCE-free.
class SvfFilter
{
public:
    enum class Mode { LP, BP, HP, Notch };

    void setSampleRate (double sr) noexcept { sampleRate = sr; update(); }
    void setCutoff (float hz)      noexcept { cutoff = hz; update(); }
    void setResonance (float r)    noexcept { res = std::clamp (r, 0.0f, 1.0f); update(); }
    void setMode (Mode m)          noexcept { mode = m; }

    void reset() noexcept { ic1eq = 0.0f; ic2eq = 0.0f; }

    float processSample (float v0) noexcept
    {
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        switch (mode)
        {
            case Mode::LP:    return v2;
            case Mode::BP:    return v1;
            case Mode::HP:    return v0 - k * v1 - v2;
            case Mode::Notch: return v0 - k * v1;
        }
        return v2;
    }

private:
    void update() noexcept
    {
        if (sampleRate <= 0.0) return;
        const float nyq = static_cast<float> (sampleRate * 0.5);
        const float fc  = std::clamp (cutoff, 20.0f, 0.99f * nyq);
        const float g   = std::tan (3.14159265358979f * fc / static_cast<float> (sampleRate));
        // res 0 -> k = 2 (Q ~ 0.5), res 1 -> k = 0.02 (high Q).
        k  = 2.0f - 1.98f * res;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double sampleRate = 44100.0;
    float  cutoff = 1000.0f;
    float  res    = 0.0f;
    Mode   mode   = Mode::LP;

    float k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1eq = 0.0f, ic2eq = 0.0f;
};
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --config Debug --target VectronTests && ctest --test-dir build -C Debug --output-on-failure`
Expected: PASS (all SVF tests + the existing Phase 1/2 tests).

- [ ] **Step 6: Commit**

```bash
git add source/dsp/filter/SvfFilter.h tests/test_svf_filter.cpp tests/CMakeLists.txt
git commit -m "feat: add JUCE-free TPT SvfFilter (LP/BP/HP/Notch)"
```

---

## Task 2: SubOscillator — sine/tri/square, octave-down (pure DSP)

**Files:**
- Create: `source/dsp/osc/SubOscillator.h`
- Create: `tests/test_sub_oscillator.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `PolyBlepOscillator` (`source/dsp/osc/PolyBlepOscillator.h`, waves `Sine, Triangle, Saw, Pulse`, `setPulseWidth`, `setFrequency`, `setSampleRate`, `reset`, `processSample`).
- Produces: `class SubOscillator`. Nested `enum class Wave { Sine, Triangle, Square };` (Square → `Pulse` @ pw 0.5). Methods: `setSampleRate(double)`, `setWave(Wave)`, `setOctave(int oct)` (−1 or −2), `setNoteFrequency(float hz)`, `noteOn()` (reset phase), `float processSample()`. Frequency = `hz · 2^oct`. Level applied by the voice, not here.

- [ ] **Step 1: Create the failing test file**

`tests/test_sub_oscillator.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/osc/SubOscillator.h"

static int risingZeroCrossings (SubOscillator& o, int n)
{
    int c = 0;
    float prev = o.processSample();
    for (int i = 1; i < n; ++i)
    {
        const float s = o.processSample();
        if (prev < 0.0f && s >= 0.0f) ++c;
        prev = s;
    }
    return c;
}

TEST_CASE ("sub octave -1 halves the frequency")
{
    SubOscillator o;
    o.setSampleRate (48000.0);
    o.setWave (SubOscillator::Wave::Sine);
    o.setNoteFrequency (440.0f);
    o.setOctave (-1);
    o.noteOn();
    const int c = risingZeroCrossings (o, 48000);   // expect ~220
    REQUIRE (c >= 218);
    REQUIRE (c <= 222);
}

TEST_CASE ("sub octave -2 quarters the frequency")
{
    SubOscillator o;
    o.setSampleRate (48000.0);
    o.setWave (SubOscillator::Wave::Sine);
    o.setNoteFrequency (440.0f);
    o.setOctave (-2);
    o.noteOn();
    const int c = risingZeroCrossings (o, 48000);   // expect ~110
    REQUIRE (c >= 108);
    REQUIRE (c <= 112);
}

TEST_CASE ("sub square has near-zero DC mean")
{
    SubOscillator o;
    o.setSampleRate (48000.0);
    o.setWave (SubOscillator::Wave::Square);
    o.setNoteFrequency (220.0f);
    o.setOctave (-1);                 // 110 Hz
    o.noteOn();
    double sum = 0.0;
    const int n = 48000;
    for (int i = 0; i < n; ++i) sum += o.processSample();
    REQUIRE (std::abs (sum / n) < 0.02);
}

TEST_CASE ("sub output stays bounded for all waves")
{
    for (auto w : { SubOscillator::Wave::Sine, SubOscillator::Wave::Triangle, SubOscillator::Wave::Square })
    {
        SubOscillator o;
        o.setSampleRate (48000.0);
        o.setWave (w);
        o.setNoteFrequency (440.0f);
        o.setOctave (-1);
        o.noteOn();
        for (int i = 0; i < 48000; ++i)
        {
            const float s = o.processSample();
            REQUIRE (s >= -1.1f);
            REQUIRE (s <=  1.1f);
        }
    }
}
```

- [ ] **Step 2: Wire the test target**

In `tests/CMakeLists.txt`, add `test_sub_oscillator.cpp` to the source list:

```cmake
add_executable(VectronTests
    test_oscillator.cpp
    test_vector_engine.cpp
    test_vector_lfo.cpp
    test_svf_filter.cpp
    test_sub_oscillator.cpp
    ${CMAKE_SOURCE_DIR}/source/dsp/osc/VectorEngine.cpp)
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake -S . -B build -DVECTRON_BUILD_TESTS=ON && cmake --build build --config Debug --target VectronTests`
Expected: FAIL — `dsp/osc/SubOscillator.h` not found.

- [ ] **Step 4: Create the header**

`source/dsp/osc/SubOscillator.h`:

```cpp
#pragma once
#include <cmath>
#include "PolyBlepOscillator.h"

// Sub oscillator: a single band-limited waveform an octave (or two) below the note.
class SubOscillator
{
public:
    enum class Wave { Sine, Triangle, Square };

    void setSampleRate (double sr) noexcept { osc.setSampleRate (sr); }

    void setWave (Wave w) noexcept
    {
        switch (w)
        {
            case Wave::Sine:     osc.setWave (PolyBlepOscillator::Wave::Sine);     break;
            case Wave::Triangle: osc.setWave (PolyBlepOscillator::Wave::Triangle); break;
            case Wave::Square:   osc.setWave (PolyBlepOscillator::Wave::Pulse);
                                 osc.setPulseWidth (0.5f);                          break;
        }
    }

    void setOctave (int oct) noexcept          { octave = oct; updateFrequency(); }  // -1 or -2
    void setNoteFrequency (float hz) noexcept  { baseHz = hz;  updateFrequency(); }
    void noteOn() noexcept                     { osc.reset (0.0f); }
    float processSample() noexcept             { return osc.processSample(); }

private:
    void updateFrequency() noexcept
    {
        osc.setFrequency (baseHz * std::pow (2.0f, static_cast<float> (octave)));
    }

    PolyBlepOscillator osc;
    float baseHz = 440.0f;
    int   octave = -1;
};
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --config Debug --target VectronTests && ctest --test-dir build -C Debug --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add source/dsp/osc/SubOscillator.h tests/test_sub_oscillator.cpp tests/CMakeLists.txt
git commit -m "feat: add SubOscillator (sine/tri/square, octave-down) over PolyBlepOscillator"
```

---

## Task 3: NoiseGenerator — color morph (white→pink→brown)

**Files:**
- Create: `source/dsp/noise/NoiseGenerator.h`
- Create: `source/dsp/noise/NoiseGenerator.cpp`
- Create: `tests/test_noise_generator.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SvfFilter` (Task 1).
- Produces (this task's slice of `NoiseGenerator`): `enum class FilterType { HP, BP, LP };` (order matches the `noise_filterType` choice). Methods used now: `setSampleRate(double)`, `setColor(float 0..1)`, `setLevel(float)`, `setTuned(bool)`, `setNoiseFilter(FilterType, float cutoffHz, float reso)`, `float processSample()`. Color morphs White(0)→Pink(0.5)→Brown(1). Output = colorOut → (tuned BP if on) → noise filter → × level. Tuned BP + S&H methods are added in Tasks 4–5; the full member set is defined here to avoid churn.

- [ ] **Step 1: Create the failing test file**

`tests/test_noise_generator.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/noise/NoiseGenerator.h"

// Ratio of first-difference energy to signal energy — a crude spectral-tilt metric.
// Larger = brighter (more HF, e.g. white); smaller = darker (e.g. brown). Scale-independent.
static float tiltRatio (NoiseGenerator& n, int numSamples)
{
    double sig = 0.0, diff = 0.0;
    float prev = n.processSample();
    for (int i = 1; i < numSamples; ++i)
    {
        const float s = n.processSample();
        sig += (double) s * s;
        const float d = s - prev;
        diff += (double) d * d;
        prev = s;
    }
    return (float) (diff / (sig + 1.0e-12));
}

static float rmsOf (NoiseGenerator& n, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = n.processSample();
        sum += (double) s * s;
    }
    return (float) std::sqrt (sum / numSamples);
}

// White in, transparent (wide-open) noise filter, tuned off, unit level.
static void configureColor (NoiseGenerator& g, float color)
{
    g.setSampleRate (48000.0);
    g.setColor (color);
    g.setTuned (false);
    g.setNoiseFilter (NoiseGenerator::FilterType::LP, 20000.0f, 0.0f);
    g.setLevel (1.0f);
}

TEST_CASE ("noise color morphs from bright (white) to dark (brown)")
{
    NoiseGenerator white, pink, brown;
    configureColor (white, 0.0f);
    configureColor (pink,  0.5f);
    configureColor (brown, 1.0f);

    const float rw = tiltRatio (white, 96000);
    const float rp = tiltRatio (pink,  96000);
    const float rb = tiltRatio (brown, 96000);

    REQUIRE (rw > rp * 1.2f);
    REQUIRE (rp > rb * 1.2f);
}

TEST_CASE ("noise output stays bounded across the color sweep")
{
    for (float c = 0.0f; c <= 1.0f; c += 0.1f)
    {
        NoiseGenerator g;
        configureColor (g, c);
        for (int i = 0; i < 48000; ++i)
        {
            const float s = g.processSample();
            REQUIRE (std::isfinite (s));
            REQUIRE (std::abs (s) < 4.0f);
        }
    }
}

TEST_CASE ("noise loudness stays in a sane range across the color sweep")
{
    NoiseGenerator w, p, b;
    configureColor (w, 0.0f);
    configureColor (p, 0.5f);
    configureColor (b, 1.0f);

    for (float r : { rmsOf (w, 48000), rmsOf (p, 48000), rmsOf (b, 48000) })
    {
        REQUIRE (r > 0.1f);
        REQUIRE (r < 3.0f);
    }
}
```

> Note on makeup gains: `kPinkScale`/`kBrownScale` (Step 4) bring each color to *roughly* unit RMS. The tilt test is scale-independent, and the loudness test uses generous `[0.1, 3.0]` bounds. If the loudness test fails, adjust `kPinkScale`/`kBrownScale` so all three RMS values land near ~0.5; final perceptual matching is a listening task in Task 7.

- [ ] **Step 2: Wire the test target**

In `tests/CMakeLists.txt`, add `test_noise_generator.cpp` and the `NoiseGenerator.cpp` source:

```cmake
add_executable(VectronTests
    test_oscillator.cpp
    test_vector_engine.cpp
    test_vector_lfo.cpp
    test_svf_filter.cpp
    test_sub_oscillator.cpp
    test_noise_generator.cpp
    ${CMAKE_SOURCE_DIR}/source/dsp/osc/VectorEngine.cpp
    ${CMAKE_SOURCE_DIR}/source/dsp/noise/NoiseGenerator.cpp)
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake -S . -B build -DVECTRON_BUILD_TESTS=ON && cmake --build build --config Debug --target VectronTests`
Expected: FAIL — `dsp/noise/NoiseGenerator.h` not found.

- [ ] **Step 4: Create the header + implementation**

`source/dsp/noise/NoiseGenerator.h` (full member set — later tasks only add method *bodies*/calls, not members):

```cpp
#pragma once
#include <cstdint>
#include "dsp/filter/SvfFilter.h"

// Featured multi-color noise generator: White->Pink->Brown morph, optional
// pitch-tracked band-pass ("tuned" noise), an always-on HP/BP/LP filter, and a
// Sample & Hold value exposed as a mod source (unrouted until Phase 5). JUCE-free.
class NoiseGenerator
{
public:
    enum class FilterType { HP, BP, LP };   // order matches the noise_filterType choice

    void setSampleRate (double sr) noexcept;
    void setColor (float c) noexcept { color = (c < 0.0f) ? 0.0f : (c > 1.0f ? 1.0f : c); }
    void setLevel (float l) noexcept { level = l; }

    void setTuned (bool on) noexcept            { tuned = on; }
    void setTunedPitch (float semis) noexcept   { tunedPitch = semis; updateTuned(); }
    void setKeytrack (float percent) noexcept   { keytrack = percent * 0.01f; updateTuned(); }
    void setNoteFrequency (float hz) noexcept   { noteHz = hz; updateTuned(); }

    void setNoiseFilter (FilterType type, float cutoffHz, float reso) noexcept;

    void setShRate (float hz) noexcept;
    void setShGlide (float g) noexcept;

    void reset() noexcept;

    float processSample() noexcept;                          // filtered, level-scaled noise
    float getSampleHold() const noexcept { return shValue; } // mod source (Phase 5)

private:
    void  updateTuned() noexcept;
    float whiteSample() noexcept;                            // deterministic LCG in [-1,1)

    double sampleRate = 44100.0;
    float  color      = 0.0f;
    float  level      = 0.0f;

    // color-path state
    float    pb0 = 0.0f, pb1 = 0.0f, pb2 = 0.0f;             // Kellet pink poles
    float    brownState = 0.0f;                              // leaky integrator
    uint32_t rng = 0x1234567u;

    // tuned band-pass
    bool      tuned      = false;
    float     tunedPitch = 0.0f;
    float     keytrack   = 1.0f;                             // 0..1
    float     noteHz     = 440.0f;
    SvfFilter tunedBp;

    // always-on noise filter
    SvfFilter noiseFilter;

    // sample & hold
    float shPhase = 0.0f, shInc = 0.0f;
    float shTarget = 0.0f, shValue = 0.0f, shGlideCoef = 1.0f;
    float shRate = 5.0f, shGlide = 0.0f;
};
```

`source/dsp/noise/NoiseGenerator.cpp`:

```cpp
#include "dsp/noise/NoiseGenerator.h"
#include <cmath>
#include <algorithm>

// Per-color makeup gains bring each color to roughly unit RMS so the morph keeps
// a steady perceived level (see design spec). Tune during the Task 7 listening pass.
static constexpr float kWhiteScale = 1.0f;
static constexpr float kPinkScale  = 0.25f;
static constexpr float kBrownScale = 10.0f;

void NoiseGenerator::setSampleRate (double sr) noexcept
{
    sampleRate = sr;
    tunedBp.setSampleRate (sr);
    tunedBp.setMode (SvfFilter::Mode::BP);
    noiseFilter.setSampleRate (sr);
    setShRate (shRate);
    setShGlide (shGlide);
    updateTuned();
}

float NoiseGenerator::whiteSample() noexcept
{
    rng = rng * 1664525u + 1013904223u;
    const float u = (float) ((rng >> 8) & 0xFFFFFFu) / 16777216.0f;   // [0,1)
    return u * 2.0f - 1.0f;                                           // [-1,1)
}

float NoiseGenerator::processSample() noexcept
{
    const float white = whiteSample();

    // Pink — Paul Kellet one-pole network.
    pb0 = 0.99765f * pb0 + white * 0.0990460f;
    pb1 = 0.96300f * pb1 + white * 0.2965164f;
    pb2 = 0.57000f * pb2 + white * 1.0526913f;
    const float pink = pb0 + pb1 + pb2 + white * 0.1848f;

    // Brown — leaky integrator.
    brownState = (brownState + 0.02f * white) / 1.02f;
    const float brown = brownState;

    const float w = white * kWhiteScale;
    const float p = pink  * kPinkScale;
    const float b = brown * kBrownScale;

    float colorOut;
    if (color <= 0.5f)
    {
        const float t = color * 2.0f;
        colorOut = w * (1.0f - t) + p * t;
    }
    else
    {
        const float t = (color - 0.5f) * 2.0f;
        colorOut = p * (1.0f - t) + b * t;
    }

    // Sample & Hold (mod source) — samples the color output; unrouted until Phase 5.
    shPhase += shInc;
    if (shPhase >= 1.0f) { shPhase -= 1.0f; shTarget = colorOut; }
    shValue += (shTarget - shValue) * shGlideCoef;

    // Tuned band-pass (optional) then the always-on noise filter.
    float out = tuned ? tunedBp.processSample (colorOut) : colorOut;
    out = noiseFilter.processSample (out);
    return out * level;
}

void NoiseGenerator::setNoiseFilter (FilterType type, float cutoffHz, float reso) noexcept
{
    SvfFilter::Mode m = SvfFilter::Mode::LP;
    switch (type)
    {
        case FilterType::HP: m = SvfFilter::Mode::HP; break;
        case FilterType::BP: m = SvfFilter::Mode::BP; break;
        case FilterType::LP: m = SvfFilter::Mode::LP; break;
    }
    noiseFilter.setMode (m);
    noiseFilter.setCutoff (cutoffHz);
    noiseFilter.setResonance (reso);
}

void NoiseGenerator::updateTuned() noexcept
{
    // Keytrack blends (in log-frequency) between a fixed A4 pivot and the note.
    const float ref     = 440.0f;
    const float logHz   = (1.0f - keytrack) * std::log (ref)
                        + keytrack * std::log (std::max (1.0f, noteHz));
    const float trackHz = std::exp (logHz);
    const float centre  = trackHz * std::pow (2.0f, tunedPitch / 12.0f);
    tunedBp.setMode (SvfFilter::Mode::BP);
    tunedBp.setResonance (0.95f);     // high Q -> narrow, "tuned"
    tunedBp.setCutoff (centre);
}

void NoiseGenerator::setShRate (float hz) noexcept
{
    shRate = hz;
    shInc = (sampleRate > 0.0) ? (float) (hz / sampleRate) : 0.0f;
}

void NoiseGenerator::setShGlide (float g) noexcept
{
    shGlide = g;
    if (g <= 0.0f || sampleRate <= 0.0)
    {
        shGlideCoef = 1.0f;
    }
    else
    {
        const float tau = g * 0.05f * (float) sampleRate;   // up to ~50 ms at glide = 1
        shGlideCoef = 1.0f - std::exp (-1.0f / tau);
    }
}

void NoiseGenerator::reset() noexcept
{
    pb0 = pb1 = pb2 = 0.0f;
    brownState = 0.0f;
    tunedBp.reset();
    noiseFilter.reset();
    shPhase = 0.0f; shTarget = 0.0f; shValue = 0.0f;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --config Debug --target VectronTests && ctest --test-dir build -C Debug --output-on-failure`
Expected: PASS. If "noise loudness stays in a sane range" fails, adjust `kPinkScale`/`kBrownScale` per the Step 1 note and re-run.

- [ ] **Step 6: Commit**

```bash
git add source/dsp/noise/NoiseGenerator.h source/dsp/noise/NoiseGenerator.cpp tests/test_noise_generator.cpp tests/CMakeLists.txt
git commit -m "feat: add NoiseGenerator white/pink/brown color morph"
```

---

## Task 4: NoiseGenerator — tuned band-pass + noise filter behavior

**Files:**
- Modify: `tests/test_noise_generator.cpp`

**Interfaces:**
- Consumes: `NoiseGenerator` from Task 3 (the tuned BP + noise filter code already exists; this task adds the tests that pin their behavior). `setTuned`, `setTunedPitch`, `setKeytrack`, `setNoteFrequency`, `setNoiseFilter` are exercised here.
- Produces: no new production code — Task 3 defined the full class. This task locks the tuned-tracking and filter-type behavior with tests. (Split from Task 3 so a reviewer can accept the color morph independently of the tuned/filter behavior.)

- [ ] **Step 1: Append the failing tests**

Append to `tests/test_noise_generator.cpp`:

```cpp
TEST_CASE ("tuned noise centroid tracks pitch")
{
    auto tiltForPitch = [] (float pitch)
    {
        NoiseGenerator g;
        g.setSampleRate (48000.0);
        g.setColor (0.0f);                                                    // white in
        g.setNoiseFilter (NoiseGenerator::FilterType::LP, 20000.0f, 0.0f);    // transparent
        g.setLevel (1.0f);
        g.setNoteFrequency (440.0f);
        g.setKeytrack (100.0f);
        g.setTunedPitch (pitch);
        g.setTuned (true);
        return tiltRatio (g, 96000);
    };
    // Higher tuned pitch -> BP centre higher -> brighter -> larger tilt ratio.
    REQUIRE (tiltForPitch (12.0f) > tiltForPitch (-12.0f));
}

TEST_CASE ("noise filter type shapes the spectrum")
{
    NoiseGenerator lp, hp;
    lp.setSampleRate (48000.0); lp.setColor (0.0f); lp.setTuned (false); lp.setLevel (1.0f);
    lp.setNoiseFilter (NoiseGenerator::FilterType::LP, 1000.0f, 0.0f);
    hp.setSampleRate (48000.0); hp.setColor (0.0f); hp.setTuned (false); hp.setLevel (1.0f);
    hp.setNoiseFilter (NoiseGenerator::FilterType::HP, 1000.0f, 0.0f);

    // LP on white -> darker (low tilt); HP on white -> brighter (high tilt).
    REQUIRE (tiltRatio (hp, 96000) > tiltRatio (lp, 96000));
}
```

- [ ] **Step 2: Run the tests**

Run: `cmake --build build --config Debug --target VectronTests && ctest --test-dir build -C Debug --output-on-failure`
Expected: PASS (the Task 3 implementation already provides tuned BP + filter behavior). If either fails, it indicates a bug in Task 3's tuned/filter code — fix in `NoiseGenerator.cpp`, not the test.

- [ ] **Step 3: Commit**

```bash
git add tests/test_noise_generator.cpp
git commit -m "test: pin NoiseGenerator tuned band-pass tracking and filter-type behavior"
```

---

## Task 5: NoiseGenerator — Sample & Hold (built, unrouted)

**Files:**
- Modify: `tests/test_noise_generator.cpp`

**Interfaces:**
- Consumes: `NoiseGenerator` from Task 3 (`setShRate`, `setShGlide`, `getSampleHold` already implemented). This task locks S&H behavior with tests.
- Produces: no new production code. The S&H value is a mod source consumed by nothing until Phase 5.

- [ ] **Step 1: Append the failing tests**

Append to `tests/test_noise_generator.cpp`:

```cpp
TEST_CASE ("sample-and-hold holds between rate ticks with no glide")
{
    NoiseGenerator g;
    g.setSampleRate (48000.0);
    g.setColor (0.0f);
    g.setShRate (10.0f);        // one step = 4800 samples
    g.setShGlide (0.0f);

    g.processSample();
    float prev = g.getSampleHold();
    int changes = 0;
    for (int i = 1; i < 4700; ++i)          // safely within the first step
    {
        g.processSample();
        const float v = g.getSampleHold();
        if (std::abs (v - prev) > 1.0e-6f) ++changes;
        prev = v;
    }
    REQUIRE (changes == 0);

    int stepChanges = 0;
    float held = prev;
    for (int i = 0; i < 96000; ++i)         // ~2 s -> ~20 steps
    {
        g.processSample();
        const float v = g.getSampleHold();
        if (std::abs (v - held) > 1.0e-6f) { ++stepChanges; held = v; }
    }
    REQUIRE (stepChanges >= 5);
}

TEST_CASE ("sample-and-hold glide smooths transitions")
{
    NoiseGenerator g;
    g.setSampleRate (48000.0);
    g.setColor (0.0f);
    g.setShRate (10.0f);
    g.setShGlide (0.8f);

    g.processSample();
    float prev = g.getSampleHold();
    float maxDelta = 0.0f, minV = 1.0e9f, maxV = -1.0e9f;
    for (int i = 0; i < 96000; ++i)
    {
        g.processSample();
        const float v = g.getSampleHold();
        maxDelta = std::max (maxDelta, std::abs (v - prev));
        minV = std::min (minV, v);
        maxV = std::max (maxV, v);
        prev = v;
    }
    REQUIRE (maxDelta < 0.1f);        // smoothed: no instant jumps
    REQUIRE (maxV - minV > 0.05f);    // but still moving over time
}
```

> These tests need `<algorithm>` for `std::max`/`std::min`. Add `#include <algorithm>` to the top of `tests/test_noise_generator.cpp` if not already present.

- [ ] **Step 2: Run the tests**

Run: `cmake --build build --config Debug --target VectronTests && ctest --test-dir build -C Debug --output-on-failure`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_noise_generator.cpp
git commit -m "test: pin NoiseGenerator sample-and-hold hold/glide behavior"
```

---

## Task 6: Parameter layout — Phase 3 params

**Files:**
- Modify: `source/params/ParameterLayout.cpp`

**Interfaces:**
- Consumes: existing `createParameterLayout()` (amp ADSR, master vol/tune, osc, vector, vector-LFO params) with `using APF/API/APC/APB` already declared.
- Produces APVTS params (PRD §8). Choice option ORDER is load-bearing:
  - `sub_wave`: choice `{Sine, Triangle, Square}` (default Sine). `sub_oct`: choice `{-1, -2}` (default −1). `sub_level`: float 0..1 (default 0).
  - `noise_color`: float 0..1 (default 0). `noise_tuned`: bool (default false). `noise_pitch`: float −24..24 st (default 0). `noise_keytrack`: float 0..100 (default 100). `noise_filterType`: choice `{HP, BP, LP}` (default LP). `noise_cutoff`: float 20..20000 Hz log (default 20000). `noise_reso`: float 0..1 (default 0). `noise_level`: float 0..1 (default 0). `noise_sh_rate`: float 0.1..50 Hz log (default 5). `noise_sh_glide`: float 0..1 (default 0).
- Defaults `sub_level`/`noise_level` = 0 so a fresh patch is unchanged from Phase 2.

- [ ] **Step 1: Add a log-range helper**

In `source/params/ParameterLayout.cpp`, add this static helper right after the existing `lfoRateRange()` function (before `createParameterLayout()`):

```cpp
    static juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r { lo, hi, 0.0f };
        r.setSkewForCentre (std::sqrt (lo * hi));   // geometric centre -> log-like taper
        return r;
    }
```

- [ ] **Step 2: Add the params**

In `source/params/ParameterLayout.cpp`, immediately before `return layout;` (after the vector-LFO loop), add:

```cpp
        // --- Sub oscillator (PRD §5.3) ---
        layout.add (std::make_unique<APC> (juce::ParameterID { "sub_wave", 1 },
            "Sub Wave", juce::StringArray { "Sine", "Triangle", "Square" }, 0));
        layout.add (std::make_unique<APC> (juce::ParameterID { "sub_oct", 1 },
            "Sub Octave", juce::StringArray { "-1", "-2" }, 0));
        layout.add (std::make_unique<APF> (juce::ParameterID { "sub_level", 1 },
            "Sub Level", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));

        // --- Noise generator (PRD §5.4) ---
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_color", 1 },
            "Noise Color", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APB> (juce::ParameterID { "noise_tuned", 1 },
            "Noise Tuned", false));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_pitch", 1 },
            "Noise Pitch", juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_keytrack", 1 },
            "Noise Keytrack", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f));
        layout.add (std::make_unique<APC> (juce::ParameterID { "noise_filterType", 1 },
            "Noise Filter Type", juce::StringArray { "HP", "BP", "LP" }, 2));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_cutoff", 1 },
            "Noise Cutoff", logRange (20.0f, 20000.0f), 20000.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_reso", 1 },
            "Noise Resonance", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_level", 1 },
            "Noise Level", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_sh_rate", 1 },
            "Noise S&H Rate", logRange (0.1f, 50.0f), 5.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "noise_sh_glide", 1 },
            "Noise S&H Glide", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
```

- [ ] **Step 3: Build the plugin to verify params compile + register**

Run: `cmake --build build --config Debug --target Vectron_Standalone`
Expected: builds clean (no dedicated test — `VectronTests` does not link JUCE; correctness is verified by the build and the host check in Task 7).

- [ ] **Step 4: Commit**

```bash
git add source/params/ParameterLayout.cpp
git commit -m "feat: add sub oscillator and noise generator parameters"
```

---

## Task 7: Wire Sub + Noise into the voice and processor

**Files:**
- Modify: `source/dsp/VectronVoice.h`, `source/dsp/VectronVoice.cpp`
- Modify: `source/PluginProcessor.h`, `source/PluginProcessor.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `SubOscillator` (Task 2), `NoiseGenerator` (Tasks 3–5), all Phase 3 params (Task 6).
- Produces: extended `VectronVoiceParams` (sub + noise fields); voice owns a `SubOscillator`, a `NoiseGenerator`, and a smoothed `subLevel`; the processor caches the new param pointers and pushes them per block. Voice render: `mix = vecOut·vectorLevel + sub·subLevel + noiseOut`, then `× env × velocity × 0.3`.

- [ ] **Step 1: Add NoiseGenerator.cpp to the plugin target**

In `CMakeLists.txt`, extend `target_sources(Vectron PRIVATE ...)`:

```cmake
target_sources(Vectron PRIVATE
    source/PluginProcessor.cpp
    source/params/ParameterLayout.cpp
    source/dsp/VectronVoice.cpp
    source/dsp/osc/VectorEngine.cpp
    source/dsp/noise/NoiseGenerator.cpp)
```

- [ ] **Step 2: Extend `VectronVoiceParams` + voice members**

In `source/dsp/VectronVoice.h`, add the two includes below the existing ones:

```cpp
#include "osc/SubOscillator.h"
#include "noise/NoiseGenerator.h"
```

In the `struct VectronVoiceParams`, add these fields at the end (after `int lfoShape[2]`):

```cpp

    // Sub oscillator
    int   subWave  { 0 };      // 0 Sine, 1 Triangle, 2 Square
    int   subOct   { 0 };      // 0 -> -1 oct, 1 -> -2 oct
    float subLevel { 0.0f };

    // Noise generator
    float noiseColor      { 0.0f };
    bool  noiseTuned      { false };
    float noisePitch      { 0.0f };
    float noiseKeytrack   { 100.0f };
    int   noiseFilterType { 2 };     // 0 HP, 1 BP, 2 LP
    float noiseCutoff     { 20000.0f };
    float noiseReso       { 0.0f };
    float noiseLevel      { 0.0f };
    float noiseShRate     { 5.0f };
    float noiseShGlide    { 0.0f };
```

In the `VectronVoice` class private members, add after `juce::SmoothedValue<float> vectorLevel;`:

```cpp
    SubOscillator subOsc;
    NoiseGenerator noiseGen;
    juce::SmoothedValue<float> subLevel;
```

- [ ] **Step 3: Update the voice implementation**

In `source/dsp/VectronVoice.cpp`, in `prepare()`, add after `vectorLevel.reset (sampleRate, 0.01);`:

```cpp
    subOsc.setSampleRate (sampleRate);
    noiseGen.setSampleRate (sampleRate);
    subLevel.reset (sampleRate, 0.01);
```

In `applyParams()`, add at the end (after `vectorLevel.setTargetValue (params.vectorLevel);`):

```cpp
    subOsc.setWave (static_cast<SubOscillator::Wave> (params.subWave));
    subOsc.setOctave (params.subOct == 0 ? -1 : -2);
    subLevel.setTargetValue (params.subLevel);

    noiseGen.setColor (params.noiseColor);
    noiseGen.setTuned (params.noiseTuned);
    noiseGen.setTunedPitch (params.noisePitch);
    noiseGen.setKeytrack (params.noiseKeytrack);
    noiseGen.setNoiseFilter (static_cast<NoiseGenerator::FilterType> (params.noiseFilterType),
                             params.noiseCutoff, params.noiseReso);
    noiseGen.setLevel (params.noiseLevel);
    noiseGen.setShRate (params.noiseShRate);
    noiseGen.setShGlide (params.noiseShGlide);
```

In `startNote()`, add after `engine.noteOn();`:

```cpp
    subOsc.setNoteFrequency ((float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, masterTuneHz));
    subOsc.noteOn();
    noiseGen.setNoteFrequency ((float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, masterTuneHz));
```

And after `vectorLevel.setCurrentAndTargetValue (params.vectorLevel);`:

```cpp
    subLevel.setCurrentAndTargetValue (params.subLevel);
```

In `renderNextBlock()`, replace the two lines:

```cpp
        const float env = ampAdsr.getNextSample();
        const float s   = engine.processSample() * vectorLevel.getNextValue() * env * level * 0.3f;
```

with:

```cpp
        const float vec   = engine.processSample() * vectorLevel.getNextValue();
        const float sub   = subOsc.processSample() * subLevel.getNextValue();
        const float noise = noiseGen.processSample();

        const float env = ampAdsr.getNextSample();
        const float s   = (vec + sub + noise) * env * level * 0.3f;
```

- [ ] **Step 4: Cache the new param pointers in the processor header**

In `source/PluginProcessor.h`, add after the vector-LFO pointer block (`pLfoShape[2]`):

```cpp

    // Sub oscillator
    std::atomic<float>* pSubWave  { nullptr };
    std::atomic<float>* pSubOct   { nullptr };
    std::atomic<float>* pSubLevel { nullptr };

    // Noise generator
    std::atomic<float>* pNoiseColor      { nullptr };
    std::atomic<float>* pNoiseTuned      { nullptr };
    std::atomic<float>* pNoisePitch      { nullptr };
    std::atomic<float>* pNoiseKeytrack   { nullptr };
    std::atomic<float>* pNoiseFilterType { nullptr };
    std::atomic<float>* pNoiseCutoff     { nullptr };
    std::atomic<float>* pNoiseReso       { nullptr };
    std::atomic<float>* pNoiseLevel      { nullptr };
    std::atomic<float>* pNoiseShRate     { nullptr };
    std::atomic<float>* pNoiseShGlide    { nullptr };
```

- [ ] **Step 5: Resolve + assert the pointers in the constructor**

In `source/PluginProcessor.cpp`, in the constructor after the vector-LFO resolve loop (after the `for (int a = 0; a < 2; ++a)` block that sets `pLfoShape[a]`), add:

```cpp
    pSubWave  = apvts.getRawParameterValue ("sub_wave");
    pSubOct   = apvts.getRawParameterValue ("sub_oct");
    pSubLevel = apvts.getRawParameterValue ("sub_level");

    pNoiseColor      = apvts.getRawParameterValue ("noise_color");
    pNoiseTuned      = apvts.getRawParameterValue ("noise_tuned");
    pNoisePitch      = apvts.getRawParameterValue ("noise_pitch");
    pNoiseKeytrack   = apvts.getRawParameterValue ("noise_keytrack");
    pNoiseFilterType = apvts.getRawParameterValue ("noise_filterType");
    pNoiseCutoff     = apvts.getRawParameterValue ("noise_cutoff");
    pNoiseReso       = apvts.getRawParameterValue ("noise_reso");
    pNoiseLevel      = apvts.getRawParameterValue ("noise_level");
    pNoiseShRate     = apvts.getRawParameterValue ("noise_sh_rate");
    pNoiseShGlide    = apvts.getRawParameterValue ("noise_sh_glide");
```

And extend the debug asserts — add after the existing `for (int a = 0; a < 2; ++a) jassert (...)` line:

```cpp
    jassert (pSubWave && pSubOct && pSubLevel);
    jassert (pNoiseColor && pNoiseTuned && pNoisePitch && pNoiseKeytrack && pNoiseFilterType
             && pNoiseCutoff && pNoiseReso && pNoiseLevel && pNoiseShRate && pNoiseShGlide);
```

- [ ] **Step 6: Read + push the new params in processBlock**

In `source/PluginProcessor.cpp` `processBlock`, after the vector-LFO read loop (`for (int a = 0; a < 2; ++a) { vp.lfoRate[a] = ...; }`) and before the `for (int i = 0; i < synth.getNumVoices(); ++i)` push loop, add:

```cpp
    vp.subWave  = (int) pSubWave->load();
    vp.subOct   = (int) pSubOct->load();
    vp.subLevel =       pSubLevel->load();

    vp.noiseColor      =        pNoiseColor->load();
    vp.noiseTuned      =        pNoiseTuned->load() > 0.5f;
    vp.noisePitch      =        pNoisePitch->load();
    vp.noiseKeytrack   =        pNoiseKeytrack->load();
    vp.noiseFilterType = (int)  pNoiseFilterType->load();
    vp.noiseCutoff     =        pNoiseCutoff->load();
    vp.noiseReso       =        pNoiseReso->load();
    vp.noiseLevel      =        pNoiseLevel->load();
    vp.noiseShRate     =        pNoiseShRate->load();
    vp.noiseShGlide    =        pNoiseShGlide->load();
```

(The existing push loop already calls `v->setVectorParams (vp);`, which applies these.)

- [ ] **Step 7: Build everything + run tests**

Run: `cmake -S . -B build -DVECTRON_BUILD_TESTS=ON && cmake --build build --config Debug --target VectronTests Vectron_Standalone && ctest --test-dir build -C Debug --output-on-failure`
Expected: plugin + tests build clean; all DSP tests PASS.

- [ ] **Step 8: Manual host verification (acceptance criterion)**

1. Launch `build/Vectron_artefacts/Debug/Standalone/Vectron.exe`; enable an audio+MIDI device.
2. In the generic editor, set `Vector Level` low or 0 to isolate the new sources.
3. Raise `Noise Level`; sweep `Noise Color` 0 → 1 and confirm you hear **white → pink → brown** (progressively darker). Sweep `Noise Cutoff` and toggle `Noise Filter Type` — confirm the filter shapes it.
4. Enable `Noise Tuned`, play notes — confirm a **pitched "wind"** that tracks the keyboard.
5. Restore `Vector Level`; raise `Sub Level` and play low notes — confirm the **sub-octave weight** thickens the tone.
6. If the noise color sweep has an obvious loudness jump, tune `kPinkScale`/`kBrownScale` in `NoiseGenerator.cpp` and rebuild.

Document PASS/observations in the Phase 3 report.

- [ ] **Step 9: Commit**

```bash
git add source/dsp/VectronVoice.h source/dsp/VectronVoice.cpp source/PluginProcessor.h source/PluginProcessor.cpp CMakeLists.txt
git commit -m "feat: wire sub oscillator and noise generator into the voice and processor"
```

---

## Verification (Phase 3 end-to-end)

- [ ] `cmake --build build --config Debug --target VectronTests && ctest --test-dir build -C Debug --output-on-failure` — all oscillator, vector, SVF, sub, and noise tests pass.
- [ ] Plugin builds Standalone + VST3 bundle with no new warnings. (VST3 post-build system-dir copy needs admin — the `.vst3` bundle itself building is sufficient; see build-env memory.)
- [ ] Manual host check (Task 7 Step 8): noise color sweep + tuned mode audibly work; sub adds weight.
- [ ] `pluginval --strictness-level 10 --validate-in-process --skip-gui-tests build/Vectron_artefacts/Debug/VST3/Vectron.vst3` passes (pluginval in scratchpad; see build-env memory).

## Notes / Decisions Locked (Phase 3)

- **SvfFilter built now, JUCE-free, reused as the Phase 4 main filter.** Resonance 0→1 maps to Q ≈ 0.5→high.
- **Noise S&H built now but unrouted** — `getSampleHold()` exposes the value; Phase 5 wires it into the mod matrix as source "Noise S&H".
- **Tuned BP and the always-on noise filter are in series** when `noise_tuned` is on (matches PRD "independent of tuned").
- **SubOscillator reuses `PolyBlepOscillator`** (Square = Pulse @ pw 0.5) for anti-aliasing + reuse.
- **Sub/noise levels default 0** so a fresh patch is identical to the Phase 2 vector synth.
- **Color makeup gains** (`kWhiteScale/kPinkScale/kBrownScale`) are approximate; final loudness matching is a listening task (Task 7 Step 8).
- **Level application:** noise level inside `NoiseGenerator`; sub level smoothed in the voice; both summed where Phase 4's filter/drive will later insert.
