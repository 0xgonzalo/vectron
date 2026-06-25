# Vectron — Hybrid Vector Synth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> **Canonical save location on execution:** `docs/superpowers/plans/2026-06-25-vectron-synth.md` (this plan-mode copy is the source of truth until then).

**Goal:** Build a polyphonic hybrid vector-synthesis instrument plugin in JUCE — a 4-oscillator XY vector engine with a vector-trajectory envelope, a subtractive backbone (filter/drive/ADSR/unison), a featured multi-color noise generator, and a full mod matrix — shipping as VST3 + Standalone (AU/CLAP later).

**Architecture:** `juce::Synthesiser` with a custom `VectronVoice`/`VectronSound`. Each voice renders a vector engine (4 PolyBLEP oscillators bilinearly crossfaded by an X/Y position) plus sub osc and noise, summed into a per-voice mixer → drive → filter → VCA. Modulation (3 ADSRs, 2 LFOs, vector LFOs, S&H, a per-voice vector-trajectory playhead) accumulates into destinations via a fixed 8-slot mod matrix. State/params via `AudioProcessorValueTreeState` (APVTS); trajectory points live in a `ValueTree` child, not as automatable params.

**Tech Stack:** JUCE 8.0.13 (via CMake `FetchContent`), C++20, CMake ≥ 3.22, `juce_dsp` (LadderFilter, Oscillator, Reverb, Chorus, Oversampling), Catch2 v3 for DSP unit tests, `pluginval` for QA, `clap-juce-extensions` (later phase).

## Global Constraints

Every task implicitly includes these (values copied verbatim from the PRD):

- **Framework:** JUCE 8.0.13 (or latest stable 8.x). **Language:** C++20. **Build:** CMake ≥ 3.22, no Projucer.
- **Project / plugin name:** `Vectron` (codename VEKTOR in the PRD is renamed). Classes `VectronVoice`, `VectronSound`, `VectronProcessor`.
- **Dependencies:** pulled via CMake `FetchContent` (no git submodules). JUCE pinned to tag `8.0.13`; Catch2 pinned to `v3.7.1`.
- **Formats (this milestone):** VST3 + Standalone only. AU + CLAP deferred to a later phase.
- **Real-time safety:** NO allocations or locks in `processBlock` or in voice render. All per-voice state pre-allocated. `juce::ScopedNoDenormals` in `processBlock`.
- **Smoothing:** `SmoothedValue<float>` on all audible continuous params (cutoff, levels, vector pos, master volume).
- **Param IDs:** `snake_case`, exactly as named in the PRD parameter table (§8). All `float` unless marked choice/bool/int.
- **QA bar:** must pass `pluginval` at strictness 10 at every milestone that adds DSP. Anti-aliasing on saw/pulse via PolyBLEP. Target ≲ 1% CPU per voice.
- **v1 editor:** `juce::GenericAudioProcessorEditor` until Phase 9 (custom GUI).

---

## Context

The repo `/Users/0xg/Documents/Coding/vectron` is empty except for the PRD (`PRD_VEKTOR_JUCE.md`) and a stray `firebase-debug.log`. This is a greenfield JUCE plugin. The PRD (§12) defines 10 sequential phases, each producing working, testable software on its own. **This document gives full bite-sized TDD detail for Phase 1 (scaffold + first sound), then task-list outlines for Phases 2–10.** Each later phase will be expanded into its own detailed plan once its predecessor builds and passes its acceptance criterion. The point of Phase 1 is the smallest end-to-end slice: a CMake project that builds a VST3/Standalone synth which plays a single in-tune oscillator through an Amp ADSR.

---

## Target File Structure (full project)

Mirrors PRD §10, namespaced to `source/`. Files created incrementally per phase; Phase 1 creates the bold ones.

```
vectron/
├── CMakeLists.txt                         ← Phase 1
├── source/
│   ├── PluginProcessor.{h,cpp}            ← Phase 1
│   ├── params/ParameterLayout.{h,cpp}     ← Phase 1 (grows each phase)
│   ├── dsp/
│   │   ├── VectronVoice.{h,cpp}           ← Phase 1
│   │   ├── VectronSound.h                 ← Phase 1
│   │   ├── osc/PolyBlepOscillator.h       ← Phase 1 (header-only, pure C++)
│   │   ├── osc/VectorEngine.{h,cpp}       ← Phase 2
│   │   ├── osc/VectorTrajectory.{h,cpp}   ← Phase 6
│   │   ├── osc/SubOscillator.{h,cpp}      ← Phase 3
│   │   ├── noise/NoiseGenerator.{h,cpp}   ← Phase 3
│   │   ├── filter/SvfFilter.{h,cpp}       ← Phase 4
│   │   ├── filter/FilterStage.{h,cpp}     ← Phase 4
│   │   ├── mod/Envelope.{h,cpp}           ← Phase 4/5
│   │   ├── mod/Lfo.{h,cpp}                ← Phase 5
│   │   └── mod/ModMatrix.{h,cpp}          ← Phase 5
│   └── gui/                                ← Phase 9
├── tests/
│   ├── CMakeLists.txt                     ← Phase 1
│   └── test_oscillator.cpp                ← Phase 1
└── presets/                                ← Phase 10
```

**Design note:** Pure-DSP leaf classes that need no JUCE (oscillators, noise color filters, SVF math, vector weight math) are kept JUCE-free and header-only or compiled into a small lib so the Catch2 test target stays lightweight and fast. JUCE-coupled glue (voice, processor, APVTS) is verified via `pluginval` + manual host testing.

---

## Phase 1 — Scaffold + Sound (FULL DETAIL)

**Acceptance criterion (PRD §12.1):** builds VST3 + Standalone, loads in a host, plays a **monophonic, in-tune** note (one oscillator → Amp ADSR → out).

### Task 1: CMake scaffold + buildable silent plugin

**Files:**
- Create: `CMakeLists.txt`
- Create: `source/PluginProcessor.h`
- Create: `source/PluginProcessor.cpp`

**Interfaces:**
- Produces: `class VectronProcessor : public juce::AudioProcessor` with public `juce::AudioProcessorValueTreeState apvts;`. `createEditor()` returns a `GenericAudioProcessorEditor`. The CMake target is named `Vectron`.

- [ ] **Step 1: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.22)
project(Vectron VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(VECTRON_BUILD_TESTS "Build Vectron unit tests" ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.13
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Vectron
    PRODUCT_NAME              "Vectron"
    COMPANY_NAME              "Vectron Audio"
    PLUGIN_MANUFACTURER_CODE  Vctn
    PLUGIN_CODE               Vec1
    FORMATS                   VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE)

juce_generate_juce_header(Vectron)

target_sources(Vectron PRIVATE
    source/PluginProcessor.cpp
    source/params/ParameterLayout.cpp
    source/dsp/VectronVoice.cpp)

target_include_directories(Vectron PRIVATE source)

target_compile_definitions(Vectron PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Vectron
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(VECTRON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Write `source/PluginProcessor.h`**

```cpp
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/VectronVoice.h"

class VectronProcessor : public juce::AudioProcessor
{
public:
    VectronProcessor();
    ~VectronProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vectron"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::Synthesiser synth;
    static constexpr int kNumVoices = 16;
    juce::SmoothedValue<float> masterGain;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VectronProcessor)
};
```

- [ ] **Step 3: Write `source/PluginProcessor.cpp` (constructor + editor + state only; sound wired in Task 4)**

```cpp
#include "PluginProcessor.h"
#include "params/ParameterLayout.h"
#include "dsp/VectronSound.h"

VectronProcessor::VectronProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", vectron::createParameterLayout())
{
    for (int i = 0; i < kNumVoices; ++i)
        synth.addVoice (new VectronVoice());
    synth.addSound (new VectronSound());
}

void VectronProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<VectronVoice*> (synth.getVoice (i)))
            v->prepare (sampleRate, samplesPerBlock);

    masterGain.reset (sampleRate, 0.02);
}

void VectronProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Push current ADSR params into voices (control rate, per block).
    const juce::ADSR::Parameters ampParams {
        apvts.getRawParameterValue ("amp_attack")->load(),
        apvts.getRawParameterValue ("amp_decay")->load(),
        apvts.getRawParameterValue ("amp_sustain")->load(),
        apvts.getRawParameterValue ("amp_release")->load() };
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<VectronVoice*> (synth.getVoice (i)))
            v->setAmpAdsr (ampParams);

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    const float volDb = apvts.getRawParameterValue ("master_volume")->load();
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (volDb, -60.0f));
    buffer.applyGainRamp (0, buffer.getNumSamples(),
                          masterGain.getCurrentValue(),
                          masterGain.skip (buffer.getNumSamples()));
}

juce::AudioProcessorEditor* VectronProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void VectronProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void VectronProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VectronProcessor();
}
```

> Note: `VectronVoice`/`VectronSound`/`createParameterLayout` are stubbed in Tasks 2–4; this file references them so it won't link until those exist. Build the empty plugin first with temporary minimal stubs OR implement Tasks 2–4 before the first full build. For an incremental green build, create empty stub headers now and fill them in subsequent tasks.

- [ ] **Step 4: Configure & build (expect link errors until Tasks 2–4 land, or use stubs)**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target Vectron_Standalone`
Expected: JUCE fetches and configures cleanly; compilation proceeds (linker resolves once Tasks 2–4 are in).

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt source/PluginProcessor.h source/PluginProcessor.cpp
git commit -m "feat: scaffold Vectron JUCE plugin (CMake + processor skeleton)"
```

---

### Task 2: PolyBLEP oscillator (TDD, pure C++)

**Files:**
- Create: `source/dsp/osc/PolyBlepOscillator.h` (header-only, no JUCE)
- Create: `tests/CMakeLists.txt`
- Test: `tests/test_oscillator.cpp`

**Interfaces:**
- Produces: `class PolyBlepOscillator` with `enum class Wave { Sine, Saw }`, `void setSampleRate(double)`, `void setFrequency(float)`, `void setWave(Wave)`, `void reset(float startPhase=0.0f)`, `float processSample()` returning a sample in ~[-1, 1] and advancing phase. (Triangle/Pulse waves added in Phase 2.)

- [ ] **Step 1: Write the failing test `tests/test_oscillator.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "dsp/osc/PolyBlepOscillator.h"

TEST_CASE ("sine oscillator runs at the requested frequency")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Sine);
    osc.setFrequency (440.0f);
    osc.reset (0.0f);

    int risingCrossings = 0;
    float prev = osc.processSample();
    for (int i = 1; i < 48000; ++i)        // exactly 1 second
    {
        const float s = osc.processSample();
        if (prev < 0.0f && s >= 0.0f) ++risingCrossings;
        prev = s;
    }
    REQUIRE (risingCrossings >= 439);
    REQUIRE (risingCrossings <= 441);
}

TEST_CASE ("saw oscillator output stays bounded")
{
    PolyBlepOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWave (PolyBlepOscillator::Wave::Saw);
    osc.setFrequency (220.0f);
    osc.reset (0.0f);

    for (int i = 0; i < 96000; ++i)
    {
        const float s = osc.processSample();
        REQUIRE (s >= -1.05f);
        REQUIRE (s <=  1.05f);
    }
}
```

- [ ] **Step 2: Write `tests/CMakeLists.txt`**

```cmake
include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1)
FetchContent_MakeAvailable(Catch2)

add_executable(VectronTests test_oscillator.cpp)
target_include_directories(VectronTests PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(VectronTests PRIVATE cxx_std_20)
target_link_libraries(VectronTests PRIVATE Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(VectronTests)
```

- [ ] **Step 3: Run the test to verify it fails (no header yet)**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target VectronTests`
Expected: FAIL — `dsp/osc/PolyBlepOscillator.h` not found.

- [ ] **Step 4: Write `source/dsp/osc/PolyBlepOscillator.h`**

```cpp
#pragma once
#include <cmath>

class PolyBlepOscillator
{
public:
    enum class Wave { Sine, Saw };

    void setSampleRate (double sr) noexcept { sampleRate = sr; updateIncrement(); }
    void setFrequency (float hz)   noexcept { frequency  = hz; updateIncrement(); }
    void setWave (Wave w)          noexcept { wave = w; }
    void reset (float startPhase = 0.0f) noexcept { phase = startPhase; }

    float processSample() noexcept
    {
        const float t = phase;
        float value = 0.0f;

        switch (wave)
        {
            case Wave::Sine:
                value = std::sin (kTwoPi * t);
                break;
            case Wave::Saw:
                value  = 2.0f * t - 1.0f;          // naive saw [-1,1]
                value -= polyBlep (t, increment);  // correct the wrap discontinuity
                break;
        }

        phase += increment;
        if (phase >= 1.0f) phase -= 1.0f;
        return value;
    }

private:
    static constexpr float kTwoPi = 6.283185307179586f;

    void updateIncrement() noexcept
    {
        increment = (sampleRate > 0.0) ? static_cast<float> (frequency / sampleRate) : 0.0f;
    }

    static float polyBlep (float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt)            { t /= dt;              return (t + t) - (t * t) - 1.0f; }
        if (t > 1.0f - dt)     { t = (t - 1.0f) / dt;  return (t * t) + (t + t) + 1.0f; }
        return 0.0f;
    }

    double sampleRate = 44100.0;
    float  frequency  = 440.0f;
    float  increment  = 0.0f;
    float  phase      = 0.0f;
    Wave   wave       = Wave::Saw;
};
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target VectronTests && ctest --test-dir build --output-on-failure`
Expected: PASS — both test cases green.

- [ ] **Step 6: Commit**

```bash
git add source/dsp/osc/PolyBlepOscillator.h tests/CMakeLists.txt tests/test_oscillator.cpp
git commit -m "feat: PolyBLEP oscillator with sine/saw + unit tests"
```

---

### Task 3: Parameter layout (APVTS)

**Files:**
- Create: `source/params/ParameterLayout.h`
- Create: `source/params/ParameterLayout.cpp`

**Interfaces:**
- Produces: `namespace vectron { juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(); }` declaring params `amp_attack`, `amp_decay`, `amp_sustain`, `amp_release` (seconds/0–1, log skew where noted), `master_volume` (dB, −60…+6, default 0), `master_tune` (Hz, 415…466, default 440). Consumed by `VectronProcessor`'s APVTS constructor (Task 1).

- [ ] **Step 1: Write `source/params/ParameterLayout.h`**

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace vectron
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
```

- [ ] **Step 2: Write `source/params/ParameterLayout.cpp`**

```cpp
#include "ParameterLayout.h"

namespace vectron
{
    using APF = juce::AudioParameterFloat;

    static juce::NormalisableRange<float> timeRange (float maxSeconds)
    {
        juce::NormalisableRange<float> r { 0.0f, maxSeconds, 0.0001f };
        r.setSkewForCentre (maxSeconds * 0.15f);   // log-ish: more resolution low
        return r;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<APF> (juce::ParameterID { "amp_attack", 1 },
            "Amp Attack",  timeRange (10.0f), 0.005f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "amp_decay", 1 },
            "Amp Decay",   timeRange (10.0f), 0.2f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "amp_sustain", 1 },
            "Amp Sustain", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.8f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "amp_release", 1 },
            "Amp Release", timeRange (15.0f), 0.3f));

        layout.add (std::make_unique<APF> (juce::ParameterID { "master_volume", 1 },
            "Master Volume", juce::NormalisableRange<float> { -60.0f, 6.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<APF> (juce::ParameterID { "master_tune", 1 },
            "Master Tune", juce::NormalisableRange<float> { 415.0f, 466.0f, 0.1f }, 440.0f));

        return layout;
    }
}
```

- [ ] **Step 3: Build to verify the layout compiles and APVTS constructs**

Run: `cmake --build build --target Vectron_Standalone`
Expected: compiles; APVTS in `VectronProcessor` now has 6 parameters.

- [ ] **Step 4: Commit**

```bash
git add source/params/ParameterLayout.h source/params/ParameterLayout.cpp
git commit -m "feat: APVTS parameter layout (amp ADSR + master volume/tune)"
```

---

### Task 4: Voice + Sound → first note, in tune

**Files:**
- Create: `source/dsp/VectronSound.h`
- Create: `source/dsp/VectronVoice.h`
- Create: `source/dsp/VectronVoice.cpp`

**Interfaces:**
- Consumes: `PolyBlepOscillator` (Task 2); `amp_*` params via `setAmpAdsr` from the processor (Task 1).
- Produces: `class VectronVoice : public juce::SynthesiserVoice` with `void prepare(double sampleRate, int blockSize)`, `void setAmpAdsr(const juce::ADSR::Parameters&)`; `struct VectronSound : public juce::SynthesiserSound`.

- [ ] **Step 1: Write `source/dsp/VectronSound.h`**

```cpp
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

struct VectronSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};
```

- [ ] **Step 2: Write `source/dsp/VectronVoice.h`**

```cpp
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "osc/PolyBlepOscillator.h"

class VectronVoice : public juce::SynthesiserVoice
{
public:
    void prepare (double sampleRate, int blockSize);
    void setAmpAdsr (const juce::ADSR::Parameters& p) { ampAdsr.setParameters (p); }

    bool canPlaySound (juce::SynthesiserSound*) override;
    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override;

private:
    PolyBlepOscillator osc;
    juce::ADSR ampAdsr;
    float level = 0.0f;
};
```

- [ ] **Step 3: Write `source/dsp/VectronVoice.cpp`**

```cpp
#include "VectronVoice.h"
#include "VectronSound.h"

void VectronVoice::prepare (double sampleRate, int /*blockSize*/)
{
    osc.setSampleRate (sampleRate);
    osc.setWave (PolyBlepOscillator::Wave::Saw);
    ampAdsr.setSampleRate (sampleRate);
}

bool VectronVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<VectronSound*> (sound) != nullptr;
}

void VectronVoice::startNote (int midiNoteNumber, float velocity,
                              juce::SynthesiserSound*, int)
{
    osc.setFrequency ((float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber));
    osc.reset (0.0f);
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
        const float env = ampAdsr.getNextSample();
        const float s   = osc.processSample() * env * level * 0.3f; // headroom trim

        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            output.addSample (ch, startSample + i, s);

        if (! ampAdsr.isActive())
        {
            clearCurrentNote();
            break;
        }
    }
}
```

- [ ] **Step 4: Build VST3 + Standalone**

Run: `cmake --build build --target Vectron_Standalone Vectron_VST3`
Expected: both targets link and build successfully.

- [ ] **Step 5: Manual verification — play a note in tune**

Run the Standalone: open `build/Vectron_artefacts/Debug/Standalone/Vectron.app` (macOS) and enable an audio+MIDI device, OR load the VST3 in AudioPluginHost.
Verify: pressing a MIDI key (e.g. A4 = MIDI 69) produces a sawtooth tone; A4 reads **440 Hz** on a tuner/spectrum analyzer; note stops with the Amp ADSR release; ADSR sliders in the generic editor change the envelope audibly. No clicks on note-off.

- [ ] **Step 6: Commit**

```bash
git add source/dsp/VectronSound.h source/dsp/VectronVoice.h source/dsp/VectronVoice.cpp
git commit -m "feat: VectronVoice/Sound wiring — plays a monophonic in-tune note"
```

---

## Phases 2–10 — Task Outlines

Each phase is expanded into its own detailed TDD plan before execution. Listed below: new files (PRD §10), the params it adds (PRD §8), and its acceptance criterion (PRD §12). **Every phase ends by running `pluginval` (strictness 10) and the manual host check before moving on.**

### Phase 2 — Vector Engine
- **Files:** `dsp/osc/VectorEngine.{h,cpp}`; extend `PolyBlepOscillator` with Triangle + Pulse (PolyBLEP/PolyBLAMP) and per-osc oct/coarse/fine/pw/level/phaseReset; wire into `VectronVoice`.
- **Params:** `oscA..D_{wave,oct,coarse,fine,pw,level,phaseReset}`, `vector_x`, `vector_y`, `vector_xfade`, `vector_level`, `vector_{x,y}Lfo{Rate,Depth,Shape}`.
- **DSP:** bilinear corner weights `gA=(1-u)v, gB=uv, gC=(1-u)(1-v), gD=u(1-v)` (u,v = normalized X,Y); Linear vs Equal-Power xfade (sqrt+renorm); dedicated per-axis vector LFOs; smoothed final X/Y.
- **Tests:** weights sum to 1 (both xfade modes); each corner isolates its osc at the corner position; osc tuning across oct/coarse/fine.
- **✅ Criterion:** moving `vector_x/y` in the generic editor audibly changes timbre.

### Phase 3 — Sub + Noise
- **Files:** `dsp/osc/SubOscillator.{h,cpp}`, `dsp/noise/NoiseGenerator.{h,cpp}`; add both to the voice mixer.
- **Params:** `sub_{wave,oct,level}`; `noise_{color,tuned,pitch,keytrack,filterType,cutoff,reso,level,sh_rate,sh_glide}`.
- **DSP:** sub (sine/tri/square, −1/−2 oct, tracks pitch). Noise color morph White→Pink(Kellet)→Brown(leaky integrator) crossfaded by `noise_color`; tuned-noise BP SVF (high-Q) tracking pitch; always-on noise filter (HP/BP/LP); S&H as a mod source sampling noise at `noise_sh_rate` with glide.
- **Tests:** noise color spectral slope sanity (white flat, pink ≈ −3 dB/oct, brown ≈ −6 dB/oct); S&H holds between sample ticks; sub octave ratios.
- **✅ Criterion:** audible variable noise (color sweep + tuned mode) and sub weight.

### Phase 4 — Filter + Drive
- **Files:** `dsp/filter/SvfFilter.{h,cpp}` (TPT/Zavalishin, LP/BP/HP/Notch), `dsp/filter/FilterStage.{h,cpp}` (wraps SVF + `juce::dsp::LadderFilter`); `dsp/mod/Envelope.{h,cpp}` (or `juce::ADSR`-based) for Filter ADSR; drive/shaper in the mixer stage.
- **Params:** `filter_{type,mode,slope,cutoff,reso,drive,keytrack,envAmount}`; `drive_{type,amount,trim,position}`; `filt_{attack,decay,sustain,release,velAmt}`.
- **DSP:** effective cutoff `= cutoff · keytrack · 2^(filterEnv·amount·octaves) + mods`; drive types Tanh/Hard/Foldback, pre/post filter position.
- **Tests:** SVF cutoff frequency response (−3 dB point); SVF stability under fast cutoff modulation; drive shaper monotonic + bounded.
- **✅ Criterion:** Filter ADSR sweeps cutoff; both filter engines and drive types audibly work.

### Phase 5 — Modulation (LFOs + Mod Matrix + Mod Env)
- **Files:** `dsp/mod/Lfo.{h,cpp}`, `dsp/mod/ModMatrix.{h,cpp}`; third (Mod) ADSR.
- **Params:** `lfo1_*`, `lfo2_*` (`shape,rate,sync,phase,fadeIn,polarity,mode`); `mod{1..8}_{src,dst,amt,en}`; `mod_{attack,decay,sustain,release,velAmt}`.
- **DSP:** fixed array of 8 `ModSlot{ sourceId, destId, amount, enabled }`; evaluate sources (control/sample rate per destination) and accumulate into destinations before applying. Sources/destinations exactly per PRD §6.3.
- **Tests:** LFO shape correctness + tempo-sync division math; matrix routing accumulates correctly (e.g. LFO1→VectorX, ModEnv→Cutoff); fade-in ramps.
- **✅ Criterion:** routing LFO→VectorX and env→cutoff works through the matrix.

### Phase 6 — Vector Trajectory ⭐
- **Files:** `dsp/osc/VectorTrajectory.{h,cpp}` (point model + per-voice playhead).
- **Params (macros, automatable):** `traj_{mode,depth,rate,sync,loopStart,loopEnd,loopDir,interp,trigger,retrigger,recPoints}`.
- **State (NOT params):** point list `P0..Pn` (≤16, default 4) `{x,y,time,tension}` stored as a `ValueTree` child of APVTS state, serialized with presets.
- **DSP:** per-voice playhead `{segIndex, segPhase, elapsed, dir}` (pre-allocated). Advance by `time·(1/traj_rate)/sampleRate`; interp `(x,y)` Linear/Smooth(+tension); modes Off/One-Shot/Loop/Loop+Sustain; loopDir Forward/Ping-Pong/Reverse; Per-Note vs Global trigger; feeds unified final X/Y model (PRD §5.2): `finalX = clamp(base_x·(1-traj_depth) + traj_x·traj_depth + vectorLFO_x + mods_x, -1, 1)`.
- **Tests:** one-shot reaches Pn and holds; loop region wraps; ping-pong flips dir; `traj_depth` blend endpoints; segment timing.
- **✅ Criterion:** a note travels the XY path and loops (points edited by hand in state for now).

### Phase 7 — Voices (poly/mono/glide/unison)
- **Files:** voice-management logic in `VectronVoice`/processor; unison stacking.
- **Params:** `poly_voices` (1–32, default 16), `voice_mode` (Poly/Mono/Legato), `glide_{mode,time}`, `bend_range` (default ±2 st); `unison_{voices,detune,spread,blend}`.
- **DSP:** voice stealing (JUCE default for v1); glide off/legato/always (log 0–2000 ms); mono/legato envelope retrigger rules; unison = N detuned/spread sub-voices per note (watch total voice count vs CPU).
- **Tests:** glide interpolates pitch over time; unison detune spread symmetric; mono legato does not retrigger envelope.
- **✅ Criterion:** chords play; fat unison; glide works in mono and poly.

### Phase 8 — Master FX
- **Files:** master FX chain post voice-sum (`juce::dsp::Chorus`, delay line, `juce::dsp::Reverb`).
- **Params:** `chorus_*` (rate/depth/mix), `delay_*` (time tempo-sync/feedback/mix/ping-pong), `reverb_*` (size/damp/width/mix); `master_volume`, `master_tune` (already present).
- **Tests:** delay time tempo-sync math; reverb/chorus bypass null; wet/dry mix law.
- **✅ Criterion:** lush pads via the FX chain.

### Phase 9 — Custom GUI
- **Files:** `source/gui/*` — XY Vector Pad (position drag + animated LFO/mod/trajectory trail; node editing for `P0..Pn`; loop-region marker; record-arm gesture capture resampled to `traj_recPoints`; animated playhead via JUCE 8 animation framework); OSC A–D strips; Sub/Noise panel (color viz); Filter; Envelopes ×3 + LFOs ×2 (curve drawing); 8-row Mod Matrix grid; Unison/Glide/Master/FX. Switch `createEditor()` from generic to custom. Aesthetic: dark/digital brutalism, mono type for values.
- **✅ Criterion:** editable + recordable XY pad with nodes, loop region, record-arm, animated playhead.

### Phase 10 — Presets + QA (+ AU/CLAP)
- **Files:** `presets/` (10–15 factory: bass, lead, pad, pluck, drone, fx — several trajectory-led); add AU + CLAP formats (`clap-juce-extensions` via FetchContent) to `juce_add_plugin`/`clap_juce_extensions_plugin`.
- **QA:** `pluginval` strictness 10 on every format; CPU profiling (≲1% per voice); aliasing spectrum sweep; bypass null test; note-on/off click checks.
- **✅ Criterion:** presets load/sound correct; passes pluginval at strictness 10 across formats.

---

## Verification (Phase 1 end-to-end)

1. **Configure + build:**
   `cmake -B build -DCMAKE_BUILD_TYPE=Debug`
   `cmake --build build` (builds `Vectron_Standalone`, `Vectron_VST3`, `VectronTests`).
2. **Unit tests:** `ctest --test-dir build --output-on-failure` → all oscillator tests pass.
3. **Manual host test:** launch the Standalone (or load the VST3 in AudioPluginHost), send MIDI A4 → hear a saw note; confirm **440 Hz** on a tuner/analyzer; confirm Amp ADSR sliders shape the note and there are no note-off clicks.
4. **(Optional, recommended) pluginval:** `pluginval --strictness-level 10 --validate build/Vectron_artefacts/Debug/VST3/Vectron.vst3` → passes.

---

## Notes / Decisions Locked

- Name **Vectron**; deps via **FetchContent**; formats **VST3 + Standalone** this milestone (AU/CLAP in Phase 10).
- Pure-DSP classes kept JUCE-free for fast Catch2 testing; JUCE-coupled glue verified via pluginval + manual host checks.
- Trajectory points are **state (`ValueTree`), never automatable params** — only the `traj_*` macros are APVTS params.
- On execution, copy this plan to `docs/superpowers/plans/2026-06-25-vectron-synth.md`.
