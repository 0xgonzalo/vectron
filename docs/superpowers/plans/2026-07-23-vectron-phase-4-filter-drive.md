# Vectron Phase 4 — Filter + Drive Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the per-voice filter stage (SVF + Ladder engines), drive/shaper (pre/post filter), and Filter ADSR with keytracking and bipolar env amount — Phase 4 of the Vectron synth (spec: `docs/superpowers/specs/2026-07-22-vectron-phase-4-filter-drive-design.md`).

**Architecture:** JUCE-free tested cores (`DriveShaper`, `SvfCascade`, `FilterMath`) + a thin JUCE-coupled `FilterStage` wrapping `SvfCascade` and `juce::dsp::LadderFilter<float>`. The voice inserts drive → filter → drive between the mixer sum and the VCA; a second `juce::ADSR` drives cutoff via `effectiveCutoffHz(...)`.

**Tech Stack:** JUCE 8.0.13 (FetchContent), C++20, CMake, Catch2 v3.7.1, pluginval.

## Global Constraints

- **Param IDs:** `snake_case`, exactly as PRD §8. Choice-param option order is load-bearing.
- **JUCE-free DSP:** `DriveShaper.h`, `SvfCascade.h`, `FilterMath.h` include NO JUCE headers — only `<cmath>`, `<algorithm>`.
- **Real-time safety:** no allocations/locks in voice render. All state pre-allocated.
- **Build (Windows, MSVC, this machine):** cmake/ctest are NOT on PATH. Use:
  `$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"`
  `$ctest = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"`
  Config is `Debug`; pass `--config Debug` to builds and `-C Debug` to ctest.
  If target `VectronTests` is missing from the cache, reconfigure once: `& $cmake -B build -DVECTRON_BUILD_TESTS=ON`.
  Do NOT build `ALL_BUILD` — the VST3 post-build copy to `C:\Program Files\Common Files\VST3` needs admin and fails. Build `Vectron_Standalone` and (for pluginval) `Vectron_VST3`; when `Vectron_VST3` errors at its final copy step, that is benign — the bundle is complete at `build/Vectron_artefacts/Debug/VST3/Vectron.vst3`.
- **Locked design decisions:** Filter env = `juce::ADSR` (exponential Envelope class deferred to Phase 5). Ladder+Notch → Ladder BP fallback. Env depth ±5 octaves at full amount. Drive amount 0 = exact identity. `filter_cutoff` default 1 kHz (PRD).
- **JUCE API gotcha:** `juce::dsp::LadderFilter<float>::processSample`/`updateSmoothers` are **protected** — expose via a private subclass with `using` declarations (Task 4 shows how).

---

### Task 1: DriveShaper (TDD, JUCE-free)

**Files:**
- Create: `source/dsp/drive/DriveShaper.h`
- Test: `tests/test_drive_shaper.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

**Interfaces:**
- Produces: `class DriveShaper` with `enum class Type { Tanh, Hard, Foldback }`, `void setType(Type)`, `void setAmount(float 0..1)` (pre-gain 1×–20×), `void setTrimDb(float)`, `void setTrimGain(float)`, `float processSample(float) const`. Stateless. Amount 0 → exact identity (trim still applies). Consumed by `VectronVoice` (Task 6).

- [ ] **Step 1: Write the failing test `tests/test_drive_shaper.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include "dsp/drive/DriveShaper.h"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE ("drive amount 0 is an exact identity for every type")
{
    for (auto type : { DriveShaper::Type::Tanh, DriveShaper::Type::Hard, DriveShaper::Type::Foldback })
    {
        DriveShaper d;
        d.setType (type);
        d.setAmount (0.0f);
        d.setTrimDb (0.0f);
        for (float x : { -2.0f, -0.5f, 0.0f, 0.3f, 1.7f })
            REQUIRE (d.processSample (x) == x);
    }
}

TEST_CASE ("tanh and hard clip are monotonic and bounded at full drive")
{
    for (auto type : { DriveShaper::Type::Tanh, DriveShaper::Type::Hard })
    {
        DriveShaper d;
        d.setType (type);
        d.setAmount (1.0f);
        d.setTrimDb (0.0f);
        float prev = d.processSample (-3.0f);
        for (float x = -2.99f; x <= 3.0f; x += 0.01f)
        {
            const float y = d.processSample (x);
            REQUIRE (y >= prev - 1.0e-6f);          // non-decreasing
            REQUIRE (std::abs (y) <= 1.0f + 1.0e-6f);
            prev = y;
        }
    }
}

TEST_CASE ("foldback folds back past the boundary and stays bounded")
{
    DriveShaper d;
    d.setType (DriveShaper::Type::Foldback);
    d.setAmount (1.0f);                              // pre-gain 20x
    d.setTrimDb (0.0f);
    // g*x = 1.0 -> exactly 1.0 ; g*x = 1.5 -> folds back down to 0.5
    REQUIRE_THAT (d.processSample (0.05f),  WithinAbs (1.0f, 1.0e-4f));
    REQUIRE_THAT (d.processSample (0.075f), WithinAbs (0.5f, 1.0e-4f));
    for (float x = -5.0f; x <= 5.0f; x += 0.001f)
        REQUIRE (std::abs (d.processSample (x)) <= 1.0f + 1.0e-5f);
}

TEST_CASE ("trim applies dB gain at the output")
{
    DriveShaper d;
    d.setType (DriveShaper::Type::Hard);
    d.setAmount (0.0f);
    d.setTrimDb (-6.0f);
    REQUIRE_THAT (d.processSample (1.0f), WithinRel (0.501187f, 1.0e-4f));
    d.setTrimDb (6.0f);
    REQUIRE_THAT (d.processSample (0.25f), WithinRel (0.25f * 1.995262f, 1.0e-4f));
    d.setTrimGain (0.5f);
    REQUIRE (d.processSample (1.0f) == 0.5f);
}
```

- [ ] **Step 2: Add the test to `tests/CMakeLists.txt`**

In the `add_executable(VectronTests ...)` list, after `test_noise_generator.cpp`, add:

```cmake
    test_drive_shaper.cpp
```

- [ ] **Step 3: Run to verify it fails**

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --build build --config Debug --target VectronTests
```
Expected: FAIL — `dsp/drive/DriveShaper.h: No such file or directory`.

- [ ] **Step 4: Write `source/dsp/drive/DriveShaper.h`**

```cpp
#pragma once
#include <cmath>
#include <algorithm>

// Waveshaper for the voice mixer stage: Tanh / Hard clip / Foldback (PRD §5.5).
// Stateless, JUCE-free. Amount 0 is an exact identity (trim still applies) so a
// fresh patch nulls against the un-driven signal path. Pre-gain = 1 + 19*amount.
class DriveShaper
{
public:
    enum class Type { Tanh, Hard, Foldback };

    void setType (Type t)        noexcept { type = t; }
    void setAmount (float a)     noexcept { amount = std::clamp (a, 0.0f, 1.0f); }
    void setTrimDb (float db)    noexcept { trimGain = std::pow (10.0f, db * 0.05f); }
    void setTrimGain (float g)   noexcept { trimGain = g; }

    float processSample (float x) const noexcept
    {
        if (amount <= 0.0f)
            return x * trimGain;

        float y = (1.0f + 19.0f * amount) * x;
        switch (type)
        {
            case Type::Tanh:     y = std::tanh (y);               break;
            case Type::Hard:     y = std::clamp (y, -1.0f, 1.0f); break;
            case Type::Foldback: y = foldback (y);                break;
        }
        return y * trimGain;
    }

private:
    // Triangle-fold into [-1, 1]: identity on [-1, 1], reflects off the
    // boundaries beyond it (period-4 triangle through the origin).
    static float foldback (float x) noexcept
    {
        const float t    = (x + 1.0f) * 0.25f;
        const float frac = t - std::floor (t);                       // [0, 1)
        return frac < 0.5f ? frac * 4.0f - 1.0f : 3.0f - frac * 4.0f;
    }

    Type  type     = Type::Tanh;
    float amount   = 0.0f;
    float trimGain = 1.0f;
};
```

- [ ] **Step 5: Run to verify it passes**

```powershell
& $cmake --build build --config Debug --target VectronTests
$ctest = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $ctest --test-dir build -C Debug --output-on-failure
```
Expected: PASS — all existing tests plus the 4 new drive cases green.

- [ ] **Step 6: Commit**

```powershell
git add source/dsp/drive/DriveShaper.h tests/test_drive_shaper.cpp tests/CMakeLists.txt
git commit -m "feat: DriveShaper (tanh/hard/foldback) with identity-at-zero + unit tests"
```

---

### Task 2: SvfCascade (TDD, JUCE-free)

**Files:**
- Create: `source/dsp/filter/SvfCascade.h`
- Test: `tests/test_svf_cascade.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

**Interfaces:**
- Consumes: `SvfFilter` (`source/dsp/filter/SvfFilter.h`, exists since Phase 3): `setSampleRate(double)`, `setCutoff(float)`, `setResonance(float 0..1)`, `setMode(SvfFilter::Mode{LP,BP,HP,Notch})`, `reset()`, `float processSample(float)`.
- Produces: `class SvfCascade` with `using Mode = SvfFilter::Mode`, `setSampleRate(double)`, `setMode(Mode)`, `setSlope24(bool)`, `setCutoff(float)`, `setResonance(float)`, `setDrive(float 0..1)`, `reset()`, `float processSample(float)`. Consumed by `FilterStage` (Task 4).

- [ ] **Step 1: Write the failing test `tests/test_svf_cascade.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "dsp/filter/SvfCascade.h"
#include "dsp/filter/SvfFilter.h"

// Peak output magnitude for a steady sine after warm-up.
static float sineMagnitude (SvfCascade& f, float freqHz, double sr = 48000.0)
{
    constexpr int warm = 4800, measure = 9600;
    const float twoPi = 6.283185307179586f;
    double phase = 0.0;
    float peak = 0.0f;
    for (int i = 0; i < warm + measure; ++i)
    {
        const float y = f.processSample (std::sin ((float) (twoPi * phase)));
        phase += freqHz / sr;
        if (phase >= 1.0) phase -= 1.0;
        if (i >= warm) peak = std::max (peak, std::abs (y));
    }
    return peak;
}

TEST_CASE ("24 dB LP rolls off much steeper than 12 dB two octaves above cutoff")
{
    SvfCascade f12, f24;
    for (auto* f : { &f12, &f24 })
    {
        f->setSampleRate (48000.0);
        f->setMode (SvfCascade::Mode::LP);
        f->setCutoff (1000.0f);
        f->setResonance (0.0f);
        f->setDrive (0.0f);
    }
    f12.setSlope24 (false);
    f24.setSlope24 (true);

    const float m12 = sineMagnitude (f12, 4000.0f);
    const float m24 = sineMagnitude (f24, 4000.0f);
    REQUIRE (m24 < m12 * 0.3f);           // theory: ~-24 dB vs ~-48 dB
    REQUIRE (m12 < 0.3f);                 // 12 dB slope is already well down
}

TEST_CASE ("cascaded notch kills the cutoff frequency, passes far below it")
{
    SvfCascade f;
    f.setSampleRate (48000.0);
    f.setMode (SvfCascade::Mode::Notch);
    f.setSlope24 (true);
    f.setCutoff (1000.0f);
    f.setResonance (0.0f);
    f.setDrive (0.0f);

    REQUIRE (sineMagnitude (f, 1000.0f) < 0.05f);
    f.reset();
    REQUIRE (sineMagnitude (f, 100.0f) > 0.7f);
}

TEST_CASE ("stable under per-sample cutoff sweeps at high resonance")
{
    SvfCascade f;
    f.setSampleRate (48000.0);
    f.setMode (SvfCascade::Mode::LP);
    f.setSlope24 (true);
    f.setResonance (0.9f);
    f.setDrive (0.0f);

    float phase = 0.0f;
    for (int i = 0; i < 96000; ++i)
    {
        // log sweep 200 Hz -> 18 kHz -> 200 Hz, updated EVERY sample
        const float sweep = (i < 48000 ? i : 96000 - i) / 48000.0f;
        f.setCutoff (200.0f * std::pow (90.0f, sweep));
        phase += 110.0f / 48000.0f;
        if (phase >= 1.0f) phase -= 1.0f;
        const float y = f.processSample (2.0f * phase - 1.0f);   // naive saw input
        REQUIRE (std::abs (y) < 10.0f);
    }
}

TEST_CASE ("12 dB path with drive 0 is bit-identical to a plain SvfFilter")
{
    SvfCascade cascade;
    SvfFilter  plain;
    cascade.setSampleRate (48000.0);  plain.setSampleRate (48000.0);
    cascade.setMode (SvfCascade::Mode::BP); plain.setMode (SvfFilter::Mode::BP);
    cascade.setSlope24 (false);
    cascade.setCutoff (2500.0f);      plain.setCutoff (2500.0f);
    cascade.setResonance (0.5f);      plain.setResonance (0.5f);
    cascade.setDrive (0.0f);

    unsigned int lcg = 1u;
    for (int i = 0; i < 4096; ++i)
    {
        lcg = lcg * 1664525u + 1013904223u;
        const float x = (float) (lcg >> 8) / 8388608.0f - 1.0f;
        REQUIRE (cascade.processSample (x) == plain.processSample (x));
    }
}
```

- [ ] **Step 2: Add `test_svf_cascade.cpp` to `tests/CMakeLists.txt`** (same list, after `test_drive_shaper.cpp`).

- [ ] **Step 3: Run to verify it fails**

Run: `& $cmake --build build --config Debug --target VectronTests`
Expected: FAIL — `dsp/filter/SvfCascade.h: No such file or directory`.

- [ ] **Step 4: Write `source/dsp/filter/SvfCascade.h`**

```cpp
#pragma once
#include <cmath>
#include "SvfFilter.h"

// 12/24 dB state-variable filter: one or two SvfFilters in series (identical
// settings, incl. Notch x2), with optional tanh pre-saturation for the
// filter_drive param (PRD §5.6 "SVF pre-gain"). JUCE-free.
class SvfCascade
{
public:
    using Mode = SvfFilter::Mode;

    void setSampleRate (double sr) noexcept { s1.setSampleRate (sr); s2.setSampleRate (sr); }
    void setMode (Mode m)          noexcept { s1.setMode (m); s2.setMode (m); }
    void setSlope24 (bool on)      noexcept { use24 = on; }
    void setCutoff (float hz)      noexcept { s1.setCutoff (hz); s2.setCutoff (hz); }
    void setResonance (float r)    noexcept { s1.setResonance (r); s2.setResonance (r); }
    void setDrive (float d)        noexcept { drive = std::clamp (d, 0.0f, 1.0f); }

    void reset() noexcept { s1.reset(); s2.reset(); }

    float processSample (float x) noexcept
    {
        if (drive > 0.0f)
            x = std::tanh ((1.0f + 3.0f * drive) * x);   // bypassed at 0: clean engine stays clean
        float y = s1.processSample (x);
        if (use24)
            y = s2.processSample (y);
        return y;
    }

private:
    SvfFilter s1, s2;
    bool  use24 = true;
    float drive = 0.0f;
};
```

- [ ] **Step 5: Run to verify it passes**

Run: `& $cmake --build build --config Debug --target VectronTests` then `& $ctest --test-dir build -C Debug --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
git add source/dsp/filter/SvfCascade.h tests/test_svf_cascade.cpp tests/CMakeLists.txt
git commit -m "feat: SvfCascade 12/24 dB SVF with tanh pre-saturation + unit tests"
```

---

### Task 3: FilterMath — effective cutoff (TDD, JUCE-free)

**Files:**
- Create: `source/dsp/filter/FilterMath.h`
- Test: `tests/test_filter_math.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

**Interfaces:**
- Produces: `namespace vectron { inline float effectiveCutoffHz (float baseHz, int midiNote, float keytrackPct, float env, float envAmount) noexcept; }` — keytrack ref note 60, env depth ±5 octaves, clamped [20, 20000]. Consumed by `VectronVoice::renderNextBlock` (Task 6).

- [ ] **Step 1: Write the failing test `tests/test_filter_math.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "dsp/filter/FilterMath.h"

using Catch::Matchers::WithinRel;
using vectron::effectiveCutoffHz;

TEST_CASE ("keytrack follows the keyboard around MIDI 60")
{
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60,  100.0f, 0.0f, 0.0f), WithinRel (1000.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 72,  100.0f, 0.0f, 0.0f), WithinRel (2000.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 48,  100.0f, 0.0f, 0.0f), WithinRel ( 500.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 72, -100.0f, 0.0f, 0.0f), WithinRel ( 500.0f, 1e-5f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 84,    0.0f, 0.0f, 0.0f), WithinRel (1000.0f, 1e-5f));
}

TEST_CASE ("envelope amount spans +/-5 octaves at full depth")
{
    REQUIRE_THAT (effectiveCutoffHz ( 500.0f, 60, 0.0f, 1.0f,  1.0f), WithinRel (16000.0f,  1e-4f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60, 0.0f, 1.0f, -1.0f), WithinRel (31.25f,    1e-4f));
    REQUIRE_THAT (effectiveCutoffHz (1000.0f, 60, 0.0f, 0.5f,  1.0f), WithinRel (5656.854f, 1e-4f));
}

TEST_CASE ("result clamps to the audio range")
{
    REQUIRE (effectiveCutoffHz (1000.0f, 60, 0.0f, 1.0f,  1.0f) == 20000.0f);   // 32k -> clamp
    REQUIRE (effectiveCutoffHz ( 100.0f, 60, 0.0f, 1.0f, -1.0f) == 20.0f);      // 3.125 -> clamp
}
```

- [ ] **Step 2: Add `test_filter_math.cpp` to `tests/CMakeLists.txt`** (same list).

- [ ] **Step 3: Run to verify it fails**

Run: `& $cmake --build build --config Debug --target VectronTests`
Expected: FAIL — `dsp/filter/FilterMath.h: No such file or directory`.

- [ ] **Step 4: Write `source/dsp/filter/FilterMath.h`**

```cpp
#pragma once
#include <cmath>
#include <algorithm>

namespace vectron
{
    // Effective filter cutoff (PRD §5.6): base * keytrack * envelope, clamped to
    // the audio range. keytrackPct in [-100, 100], reference note = MIDI 60.
    // env = Filter ADSR (0..1) already scaled by velocity; envAmount in [-1, 1]
    // maps to +/-5 octaves at full amount (locked in the Phase 4 design spec).
    inline float effectiveCutoffHz (float baseHz, int midiNote, float keytrackPct,
                                    float env, float envAmount) noexcept
    {
        constexpr float kEnvOctaves = 5.0f;
        const float keyOct = (keytrackPct * 0.01f) * (float) (midiNote - 60) / 12.0f;
        const float envOct = env * envAmount * kEnvOctaves;
        return std::clamp (baseHz * std::exp2 (keyOct + envOct), 20.0f, 20000.0f);
    }
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `& $cmake --build build --config Debug --target VectronTests` then `& $ctest --test-dir build -C Debug --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
git add source/dsp/filter/FilterMath.h tests/test_filter_math.cpp tests/CMakeLists.txt
git commit -m "feat: effective-cutoff math (keytrack + env, +/-5 oct) + unit tests"
```

---

### Task 4: FilterStage (JUCE-coupled engine switch)

No Catch2 test (JUCE-coupled, like VectronVoice) — compile-verified here, exercised by pluginval + manual check in Task 6.

**Files:**
- Create: `source/dsp/filter/FilterStage.h`
- Create: `source/dsp/filter/FilterStage.cpp`
- Modify: `CMakeLists.txt:31-36` (add `FilterStage.cpp` to `target_sources`)

**Interfaces:**
- Consumes: `SvfCascade` (Task 2), `juce::dsp::LadderFilter<float>` (protected per-sample API — exposed via subclass).
- Produces: `class FilterStage` with `enum class Engine { SVF, Ladder }`, `enum class Mode { LP, BP, HP, Notch }` (same order as `SvfFilter::Mode` and the `filter_mode` param), `void prepare(double sampleRate)`, `void setEngine(Engine)` (resets on change), `void setMode(Mode)`, `void setSlope24(bool)`, `void setCutoff(float hz)` (per-sample safe), `void setResonance(float 0..1)`, `void setDrive(float 0..1)` (control-rate only — Ladder setDrive is not per-sample cheap), `void reset()`, `float processSample(float)`. Consumed by `VectronVoice` (Task 6).

- [ ] **Step 1: Write `source/dsp/filter/FilterStage.h`**

```cpp
#pragma once
#include <juce_dsp/juce_dsp.h>
#include "SvfCascade.h"

// Per-voice main filter (PRD §5.6): switchable SVF (clean, 12/24 dB, incl.
// Notch) or juce::dsp::LadderFilter (Moog-style). Ladder has no Notch — falls
// back to Ladder BP at the selected slope (locked design decision).
class FilterStage
{
public:
    enum class Engine { SVF, Ladder };
    enum class Mode   { LP, BP, HP, Notch };   // order == filter_mode param == SvfFilter::Mode

    void prepare (double sampleRate);
    void setEngine (Engine e) noexcept;        // resets state when the engine changes
    void setMode (Mode m) noexcept;
    void setSlope24 (bool on) noexcept;
    void setCutoff (float hz) noexcept;        // already-modulated value, called per sample
    void setResonance (float r) noexcept;
    void setDrive (float d) noexcept;          // 0..1; control-rate only
    void reset() noexcept;
    float processSample (float x) noexcept;

private:
    // LadderFilter's per-sample API is protected; expose it for single-sample use.
    struct PerSampleLadder : juce::dsp::LadderFilter<float>
    {
        using juce::dsp::LadderFilter<float>::processSample;
        using juce::dsp::LadderFilter<float>::updateSmoothers;
    };

    void updateLadderMode() noexcept;

    SvfCascade      svf;
    PerSampleLadder ladder;
    Engine engine  = Engine::SVF;
    Mode   mode    = Mode::LP;
    bool   slope24 = true;
};
```

- [ ] **Step 2: Write `source/dsp/filter/FilterStage.cpp`**

```cpp
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
```

- [ ] **Step 3: Add to `CMakeLists.txt` `target_sources`** (after `source/dsp/noise/NoiseGenerator.cpp`):

```cmake
    source/dsp/filter/FilterStage.cpp
```

- [ ] **Step 4: Build to verify it compiles**

Run: `& $cmake --build build --config Debug --target Vectron_Standalone`
Expected: builds clean (FilterStage compiles and links; nothing consumes it yet).

- [ ] **Step 5: Commit**

```powershell
git add source/dsp/filter/FilterStage.h source/dsp/filter/FilterStage.cpp CMakeLists.txt
git commit -m "feat: FilterStage engine switch (SvfCascade + juce LadderFilter)"
```

---

### Task 5: Phase 4 parameters (APVTS)

**Files:**
- Modify: `source/params/ParameterLayout.cpp` (append inside `createParameterLayout`, after the noise block at line ~127, before `return layout;`)

**Interfaces:**
- Produces: 17 params — `filter_type`, `filter_mode`, `filter_slope`, `filter_cutoff`, `filter_reso`, `filter_drive`, `filter_keytrack`, `filter_envAmount`, `drive_type`, `drive_amount`, `drive_trim`, `drive_position`, `filt_attack`, `filt_decay`, `filt_sustain`, `filt_release`, `filt_velAmt`. Consumed by processor pointers (Task 6). Uses the existing `timeRange(...)` and `logRange(...)` helpers in this file.

- [ ] **Step 1: Append the parameter block**

Insert before `return layout;`:

```cpp
        // --- Filter (PRD §5.6) ---
        layout.add (std::make_unique<APC> (juce::ParameterID { "filter_type", 1 },
            "Filter Type", juce::StringArray { "SVF", "Ladder" }, 0));
        layout.add (std::make_unique<APC> (juce::ParameterID { "filter_mode", 1 },
            "Filter Mode", juce::StringArray { "LP", "BP", "HP", "Notch" }, 0));
        layout.add (std::make_unique<APC> (juce::ParameterID { "filter_slope", 1 },
            "Filter Slope", juce::StringArray { "12 dB", "24 dB" }, 1));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_cutoff", 1 },
            "Filter Cutoff", logRange (20.0f, 20000.0f), 1000.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_reso", 1 },
            "Filter Resonance", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_drive", 1 },
            "Filter Drive", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_keytrack", 1 },
            "Filter Keytrack", juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filter_envAmount", 1 },
            "Filter Env Amount", juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f));

        // --- Drive / shaper (PRD §5.5) ---
        layout.add (std::make_unique<APC> (juce::ParameterID { "drive_type", 1 },
            "Drive Type", juce::StringArray { "Tanh", "Hard clip", "Foldback" }, 0));
        layout.add (std::make_unique<APF> (juce::ParameterID { "drive_amount", 1 },
            "Drive Amount", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "drive_trim", 1 },
            "Drive Trim", juce::NormalisableRange<float> { -24.0f, 6.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<APC> (juce::ParameterID { "drive_position", 1 },
            "Drive Position", juce::StringArray { "Pre-filter", "Post-filter" }, 0));

        // --- Filter envelope (PRD §6.1) ---
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_attack", 1 },
            "Filter Attack",  timeRange (10.0f), 0.005f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_decay", 1 },
            "Filter Decay",   timeRange (10.0f), 0.2f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_sustain", 1 },
            "Filter Sustain", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.8f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_release", 1 },
            "Filter Release", timeRange (15.0f), 0.3f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "filt_velAmt", 1 },
            "Filter Env Velocity", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
```

- [ ] **Step 2: Build to verify APVTS constructs**

Run: `& $cmake --build build --config Debug --target Vectron_Standalone`
Expected: builds clean; the generic editor now lists the 17 new params.

- [ ] **Step 3: Commit**

```powershell
git add source/params/ParameterLayout.cpp
git commit -m "feat: filter/drive/filter-env parameters (PRD §8)"
```

---

### Task 6: Wire drive + filter + Filter ADSR into voice and processor

**Files:**
- Modify: `source/dsp/VectronVoice.h`
- Modify: `source/dsp/VectronVoice.cpp`
- Modify: `source/PluginProcessor.h:83` (new pointer members)
- Modify: `source/PluginProcessor.cpp` (resolve pointers, fill params per block)

**Interfaces:**
- Consumes: `FilterStage` (Task 4), `DriveShaper` (Task 1), `vectron::effectiveCutoffHz` (Task 3), params (Task 5).
- Produces: `VectronVoice::setFiltAdsr(const juce::ADSR::Parameters&)`; extended `VectronVoiceParams` fields (below). Signal chain: mix → [drive if Pre] → filter → [drive if Post] → VCA.

- [ ] **Step 1: Extend `VectronVoiceParams` in `source/dsp/VectronVoice.h`**

Add includes at the top (after the noise include):

```cpp
#include "filter/FilterStage.h"
#include "drive/DriveShaper.h"
```

Append to `struct VectronVoiceParams` (after `noiseShGlide`):

```cpp
    // Filter + drive (Phase 4)
    int   filterType      { 0 };       // 0 SVF, 1 Ladder
    int   filterMode      { 0 };       // 0 LP, 1 BP, 2 HP, 3 Notch
    int   filterSlope     { 1 };       // 0 -> 12 dB, 1 -> 24 dB
    float filterCutoff    { 1000.0f };
    float filterReso      { 0.0f };
    float filterDrive     { 0.0f };
    float filterKeytrack  { 0.0f };    // -100 .. +100 %
    float filterEnvAmount { 0.0f };    // -1 .. +1
    int   driveType       { 0 };       // 0 Tanh, 1 Hard, 2 Foldback
    float driveAmount     { 0.0f };
    float driveTrimDb     { 0.0f };
    int   drivePosition   { 0 };       // 0 Pre-filter, 1 Post-filter
    float filtVelAmt      { 0.0f };
```

In `class VectronVoice`, add next to `setAmpAdsr`:

```cpp
    void setFiltAdsr (const juce::ADSR::Parameters& p) { filtAdsr.setParameters (p); }
```

Add private members (after `subLevel`):

```cpp
    FilterStage filterStage;
    DriveShaper driveShaper;
    juce::ADSR  filtAdsr;
    juce::SmoothedValue<float> filterCutoffHz, filterReso, driveAmount, driveTrimGain;
    int currentNote = 60;
```

- [ ] **Step 2: Update `source/dsp/VectronVoice.cpp`**

`prepare` — add before `applyParams();`:

```cpp
    filterStage.prepare (sampleRate);
    filtAdsr.setSampleRate (sampleRate);
    filterCutoffHz.reset (sampleRate, 0.02);
    filterReso.reset (sampleRate, 0.01);
    driveAmount.reset (sampleRate, 0.01);
    driveTrimGain.reset (sampleRate, 0.01);
```

`applyParams` — append at the end:

```cpp
    filterStage.setEngine (static_cast<FilterStage::Engine> (params.filterType));
    filterStage.setMode (static_cast<FilterStage::Mode> (params.filterMode));
    filterStage.setSlope24 (params.filterSlope == 1);
    filterStage.setDrive (params.filterDrive);
    filterCutoffHz.setTargetValue (params.filterCutoff);
    filterReso.setTargetValue (params.filterReso);

    driveShaper.setType (static_cast<DriveShaper::Type> (params.driveType));
    driveAmount.setTargetValue (params.driveAmount);
    driveTrimGain.setTargetValue (juce::Decibels::decibelsToGain (params.driveTrimDb));
```

`startNote` — add after `subLevel.setCurrentAndTargetValue (...)`:

```cpp
    currentNote = midiNoteNumber;
    filterStage.reset();
    filterCutoffHz.setCurrentAndTargetValue (params.filterCutoff);
    filterReso.setCurrentAndTargetValue (params.filterReso);
    driveAmount.setCurrentAndTargetValue (params.driveAmount);
    driveTrimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.driveTrimDb));
    filtAdsr.noteOn();
```

`stopNote` — mirror the amp env: in the tail-off branch add `filtAdsr.noteOff();`, in the kill branch add `filtAdsr.reset();`.

`renderNextBlock` — replace the mixing/output section of the loop (currently `const float env = ...; const float s = (vec + sub + noise) * env * level * 0.3f;`) with:

```cpp
        // Filter cutoff modulation: keytrack + Filter ADSR scaled by velocity.
        const float fEnv     = filtAdsr.getNextSample()
                             * (1.0f - params.filtVelAmt + params.filtVelAmt * level);
        filterStage.setResonance (filterReso.getNextValue());
        filterStage.setCutoff (vectron::effectiveCutoffHz (filterCutoffHz.getNextValue(),
                                                           currentNote,
                                                           params.filterKeytrack,
                                                           fEnv,
                                                           params.filterEnvAmount));
        driveShaper.setAmount (driveAmount.getNextValue());
        driveShaper.setTrimGain (driveTrimGain.getNextValue());

        float s = vec + sub + noise;
        if (params.drivePosition == 0) s = driveShaper.processSample (s);
        s = filterStage.processSample (s);
        if (params.drivePosition == 1) s = driveShaper.processSample (s);

        const float env = ampAdsr.getNextSample();
        s *= env * level * 0.3f;
```

Add the include at the top of `VectronVoice.cpp`:

```cpp
#include "filter/FilterMath.h"
```

- [ ] **Step 3: Extend `source/PluginProcessor.h`**

After the noise pointer block (line ~83), add:

```cpp
    // Filter + drive + filter env
    std::atomic<float>* pFilterType      { nullptr };
    std::atomic<float>* pFilterMode      { nullptr };
    std::atomic<float>* pFilterSlope     { nullptr };
    std::atomic<float>* pFilterCutoff    { nullptr };
    std::atomic<float>* pFilterReso      { nullptr };
    std::atomic<float>* pFilterDrive     { nullptr };
    std::atomic<float>* pFilterKeytrack  { nullptr };
    std::atomic<float>* pFilterEnvAmount { nullptr };
    std::atomic<float>* pDriveType       { nullptr };
    std::atomic<float>* pDriveAmount     { nullptr };
    std::atomic<float>* pDriveTrim       { nullptr };
    std::atomic<float>* pDrivePosition   { nullptr };
    std::atomic<float>* pFiltAttack      { nullptr };
    std::atomic<float>* pFiltDecay       { nullptr };
    std::atomic<float>* pFiltSustain     { nullptr };
    std::atomic<float>* pFiltRelease     { nullptr };
    std::atomic<float>* pFiltVelAmt      { nullptr };
```

- [ ] **Step 4: Extend `source/PluginProcessor.cpp`**

Constructor — after the noise pointer resolution block:

```cpp
    pFilterType      = apvts.getRawParameterValue ("filter_type");
    pFilterMode      = apvts.getRawParameterValue ("filter_mode");
    pFilterSlope     = apvts.getRawParameterValue ("filter_slope");
    pFilterCutoff    = apvts.getRawParameterValue ("filter_cutoff");
    pFilterReso      = apvts.getRawParameterValue ("filter_reso");
    pFilterDrive     = apvts.getRawParameterValue ("filter_drive");
    pFilterKeytrack  = apvts.getRawParameterValue ("filter_keytrack");
    pFilterEnvAmount = apvts.getRawParameterValue ("filter_envAmount");
    pDriveType       = apvts.getRawParameterValue ("drive_type");
    pDriveAmount     = apvts.getRawParameterValue ("drive_amount");
    pDriveTrim       = apvts.getRawParameterValue ("drive_trim");
    pDrivePosition   = apvts.getRawParameterValue ("drive_position");
    pFiltAttack      = apvts.getRawParameterValue ("filt_attack");
    pFiltDecay       = apvts.getRawParameterValue ("filt_decay");
    pFiltSustain     = apvts.getRawParameterValue ("filt_sustain");
    pFiltRelease     = apvts.getRawParameterValue ("filt_release");
    pFiltVelAmt      = apvts.getRawParameterValue ("filt_velAmt");
```

And extend the jassert block:

```cpp
    jassert (pFilterType && pFilterMode && pFilterSlope && pFilterCutoff && pFilterReso
             && pFilterDrive && pFilterKeytrack && pFilterEnvAmount);
    jassert (pDriveType && pDriveAmount && pDriveTrim && pDrivePosition);
    jassert (pFiltAttack && pFiltDecay && pFiltSustain && pFiltRelease && pFiltVelAmt);
```

`processBlock` — after the `vp.noise*` block, add:

```cpp
    vp.filterType      = (int) pFilterType->load();
    vp.filterMode      = (int) pFilterMode->load();
    vp.filterSlope     = (int) pFilterSlope->load();
    vp.filterCutoff    =       pFilterCutoff->load();
    vp.filterReso      =       pFilterReso->load();
    vp.filterDrive     =       pFilterDrive->load();
    vp.filterKeytrack  =       pFilterKeytrack->load();
    vp.filterEnvAmount =       pFilterEnvAmount->load();
    vp.driveType       = (int) pDriveType->load();
    vp.driveAmount     =       pDriveAmount->load();
    vp.driveTrimDb     =       pDriveTrim->load();
    vp.drivePosition   = (int) pDrivePosition->load();
    vp.filtVelAmt      =       pFiltVelAmt->load();

    const juce::ADSR::Parameters filtParams {
        pFiltAttack->load(),
        pFiltDecay->load(),
        pFiltSustain->load(),
        pFiltRelease->load() };
```

And inside the voice loop, next to `v->setAmpAdsr (ampParams);`:

```cpp
            v->setFiltAdsr (filtParams);
```

- [ ] **Step 5: Build everything + run all tests**

```powershell
& $cmake --build build --config Debug --target Vectron_Standalone
& $cmake --build build --config Debug --target VectronTests
& $ctest --test-dir build -C Debug --output-on-failure
```
Expected: both build clean; all tests (existing + 3 new files) PASS.

- [ ] **Step 6: Build VST3 + pluginval strictness 10**

```powershell
& $cmake --build build --config Debug --target Vectron_VST3
```
Expected: compiles and links; the final post-build COPY step may fail with access-denied — benign, the bundle exists at `build/Vectron_artefacts/Debug/VST3/Vectron.vst3`.

pluginval is not installed. Download and run:

```powershell
$pv = "$env:TEMP\pluginval"
if (-not (Test-Path "$pv\pluginval.exe")) {
    New-Item -ItemType Directory -Force $pv | Out-Null
    Invoke-WebRequest "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip" -OutFile "$pv\pluginval.zip"
    Expand-Archive "$pv\pluginval.zip" $pv -Force
}
& "$pv\pluginval.exe" --strictness-level 10 --validate-in-process --skip-gui-tests "build\Vectron_artefacts\Debug\VST3\Vectron.vst3"
```
Expected: `ALL TESTS PASSED`.

- [ ] **Step 7: Manual acceptance check (PRD §12.4)** — launch `build\Vectron_artefacts\Debug\Standalone\Vectron.exe` and verify with the user:
  1. Raise `filter_envAmount` to +1, short `filt_decay` (~0.2 s), sustain ~0 → each note gets an audible cutoff sweep.
  2. Switch `filter_type` SVF ↔ Ladder → character change; Ladder self-oscillates near `filter_reso` = 1.
  3. Sweep `drive_amount` with each `drive_type` (Tanh/Hard clip/Foldback) → three distinct distortion characters; `drive_trim` compensates level.
  4. Flip `drive_position` Pre ↔ Post with high resonance → audible difference.

- [ ] **Step 8: Commit**

```powershell
git add source/dsp/VectronVoice.h source/dsp/VectronVoice.cpp source/PluginProcessor.h source/PluginProcessor.cpp
git commit -m "feat: wire drive, filter stage and filter ADSR into the voice and processor"
```

---

## Self-review notes

- Spec coverage: DriveShaper (T1), SvfCascade + filter_drive pre-sat (T2), cutoff math (T3), FilterStage + Notch fallback + reset rules (T4), all 17 params (T5), voice/processor wiring + smoothing + velocity scaling + pluginval + manual criterion (T6). Exponential envelopes and `+ mods` term are explicitly out of scope (Phase 5).
- Type consistency: `FilterStage::Mode` order matches `SvfFilter::Mode` and the `filter_mode` StringArray; `VectronVoiceParams` field names match the processor's `vp.*` fills; `setFiltAdsr` matches the voice declaration.
