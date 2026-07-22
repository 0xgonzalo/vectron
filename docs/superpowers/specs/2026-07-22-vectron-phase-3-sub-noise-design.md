# Vectron Phase 3 — Sub + Noise (Design Spec)

**Status:** Approved design; ready to expand into a TDD implementation plan.
**Date:** 2026-07-22
**Predecessor:** Phase 2 (Vector Engine) — complete, tests pass, pluginval strictness 10 pass. Manual audio check pending user.

## Goal

Add the tonal/textural layer that keeps Vectron musical, not just avant-garde: a **sub oscillator** (weight/body) and a **first-class, variable noise generator** (color morph, tuned mode, dedicated filter, and a Sample & Hold value for later modulation). Both sum into the per-voice mixer alongside the vector engine output.

**Acceptance criterion (PRD §12.3):** audible variable noise (color sweep + tuned mode) and audible sub weight.

## Global constraints (inherited)

- **Framework:** JUCE 8.0.13. **Language:** C++20. **Build:** CMake ≥ 3.22, no Projucer.
- **JUCE-free DSP:** leaf DSP classes (`SvfFilter`, `SubOscillator`, `NoiseGenerator`) include **no** JUCE headers — unit-tested by `VectronTests` (Catch2 only). Use `<cmath>`, `<algorithm>`, `<cstdint>`.
- **Real-time safety:** no allocations/locks in `processBlock` or voice render. Noise RNG is a deterministic LCG (RT-safe), matching `VectorLfo`.
- **Param IDs:** `snake_case`, exactly as in PRD §8. Choice-param option order is load-bearing.
- **Smoothing:** `SmoothedValue<float>` on audible continuous params (levels, cutoff).

## Design decisions (locked during brainstorming)

1. **SVF dependency:** Phase 3's noise module needs SVF filtering (tuned BP + always-on noise filter), which the roadmap slots for Phase 4. **Decision: build a JUCE-free `SvfFilter` now** (`source/dsp/filter/SvfFilter.h`), unit-tested, and **reuse the exact same class as the main filter in Phase 4.** Most consistent with the project's "pure DSP, JUCE-free, tested" principle; no throwaway work.
2. **Noise S&H:** defined as a mod *source* ("Noise S&H"), but the mod matrix arrives in Phase 5. **Decision: build the S&H generator now with its params (`noise_sh_rate`, `noise_sh_glide`), unit-tested, but unrouted** — it produces a held value each sample that nothing consumes until Phase 5 wires it into the matrix. Keeps the DSP done and tested where the roadmap places the params.
3. **Tuned + noise filter are in series:** when `noise_tuned` is on, the tuned BP (pitch-tracked) and the always-on noise filter **both** apply, in series. Matches PRD ("noise filter always active, independent of tuned").
4. **SubOscillator reuses `PolyBlepOscillator`** (square = `Pulse` @ pw 0.5) for anti-aliasing and code reuse, rather than a standalone naive oscillator.

## Architecture

### New files

| File | Purpose | Depends on |
|---|---|---|
| `source/dsp/filter/SvfFilter.h` | TPT/Zavalishin state-variable filter; one input → LP/BP/HP/Notch. Header-only, JUCE-free. Pulled forward from Phase 4; reused verbatim there. | `<cmath>` |
| `source/dsp/osc/SubOscillator.h` | Sub voice: sine/tri/square, −1/−2 oct, tracks note pitch. Wraps a `PolyBlepOscillator`. | `PolyBlepOscillator.h` |
| `source/dsp/noise/NoiseGenerator.h` / `.cpp` | White→Pink→Brown crossfade; optional tuned BP; always-on noise filter; level; internal S&H value. JUCE-free. | `SvfFilter.h`, `<cstdint>` |
| `tests/test_svf_filter.cpp` | SVF unit tests. | Catch2 |
| `tests/test_sub_oscillator.cpp` | Sub osc unit tests. | Catch2 |
| `tests/test_noise_generator.cpp` | Noise unit tests. | Catch2 |

### Modified files

- `source/params/ParameterLayout.cpp` — add all Phase 3 params.
- `source/dsp/VectronVoice.h` / `.cpp` — add `SubOscillator` + `NoiseGenerator` members; extend `VectronVoiceParams`; sum into the mixer.
- `source/PluginProcessor.cpp` — read Phase 3 params, push per block.
- `CMakeLists.txt` — add `NoiseGenerator.cpp` to the `Vectron` target.
- `tests/CMakeLists.txt` — add the three new test files + `NoiseGenerator.cpp`.

### Signal flow

**NoiseGenerator, per sample:**
```
color-morph (white/pink/brown) ──> [tuned BP if noise_tuned] ──> noise filter (HP/BP/LP, always) ──> × noise_level ──> out
        │
        └─> S&H sampler (rate + glide) ──> held value (getter; unused until Phase 5)
```

**Voice mixer (per sample):**
```
mix = vecOut·vectorLevel + sub·subLevel + noiseOut
out = mix · env · velocity · 0.3
```
Sum happens exactly where Phase 4's filter/drive will later insert (between mix and the VCA).

## DSP details

### SvfFilter (TPT / Zavalishin)
- API: `setSampleRate(double)`, `setCutoff(float hz)`, `setResonance(float 0..1)`, `setMode(LP|BP|HP|Notch)`, `float processSample(float x)`, `reset()`.
- Prewarp: `g = tan(π·fc/fs)`. Damping `k = 2 − 2·r` from resonance (clamp `r` below the self-oscillation edge for stability). States `s1, s2`.
- One-sample update; outputs: LP = `v2`, BP = `v1`, HP = `x − k·v1 − v2`, Notch = `x − k·v1`.
- `reset()` zeroes states. Cutoff clamped to `[20, min(20000, 0.45·fs)]`.

### SubOscillator
- Holds a `PolyBlepOscillator`. `setWave(Sine|Triangle|Square)` → Square maps to `Pulse` at pw 0.5.
- `setNoteFrequency(baseHz)` + `setOctave(-1|-2)` → osc freq = `baseHz · 2^oct`.
- `noteOn()` resets phase. `processSample()` returns the band-limited sample; **level applied by the voice mixer** (consistent with vecOut).

### NoiseGenerator
- **White:** deterministic LCG (as in `VectorLfo`), scaled to [−1, 1].
- **Pink:** Paul Kellet one-pole network fed by white.
- **Brown:** leaky integrator `y += (white − y)·coef`, normalized toward unit range.
- **Color morph:** crossfade white↔pink over `noise_color ∈ [0, 0.5]`, pink↔brown over `[0.5, 1]`. Each color pre-scaled by a fixed RMS-normalization constant so perceived loudness stays roughly flat across the sweep (no jump).
- **Tuned BP:** `SvfFilter` in BP mode, high Q; cutoff = `noteHz · 2^(noise_pitch/12)`, with `noise_keytrack` (0–100%) blending between a fixed reference pitch and full note tracking.
- **Noise filter:** a second `SvfFilter`; mode from `noise_filterType`, `noise_cutoff`, `noise_reso`.
- **S&H:** samples the color-morph output every `1/noise_sh_rate` seconds into a held value; `noise_sh_glide` one-pole-smooths transitions. Exposed via a getter for Phase 5; unused now.

### Parameters (APVTS — exact PRD §8 IDs)

| ID | Type | Range / options | Default |
|---|---|---|---|
| `sub_wave` | choice | Sine, Triangle, Square | Sine |
| `sub_oct` | choice | −1, −2 | −1 |
| `sub_level` | float | 0 … 1 | 0 |
| `noise_color` | float | 0 … 1 (White→Pink→Brown) | 0 |
| `noise_tuned` | bool | — | false |
| `noise_pitch` | float | −24 … +24 st | 0 |
| `noise_keytrack` | float | 0 … 100 % | 100 |
| `noise_filterType` | choice | HP, BP, LP | LP |
| `noise_cutoff` | float | 20 … 20000 Hz, log | 20000 |
| `noise_reso` | float | 0 … 1 | 0 |
| `noise_level` | float | 0 … 1 | 0 |
| `noise_sh_rate` | float | 0.1 … 50 Hz | 5 |
| `noise_sh_glide` | float | 0 … 1 | 0 |

Defaults: `sub_level`/`noise_level` = 0 so a fresh patch still sounds like the Phase 2 vector synth (PRD §3: "everything at zero → decent analog synth"); the user opens sub/noise on demand.

## Testing

**Unit (Catch2, JUCE-free):**
- `test_svf_filter.cpp`: LP −3 dB point near cutoff; BP peak at cutoff; HP/LP roughly complementary; bounded output under fast cutoff sweeps; Notch attenuates at cutoff.
- `test_sub_oscillator.cpp`: octave ratios (−1 → ½ freq, −2 → ¼ freq via zero-crossing counts); square (Pulse@0.5) DC mean ≈ 0; bounded output.
- `test_noise_generator.cpp`: spectral slope sanity (white flat, pink ≈ −3 dB/oct, brown ≈ −6 dB/oct via low-vs-high band energy ratio); bounded output across full color sweep; morph endpoints isolate white/brown; S&H holds between ticks, changes across ticks, glide smooths; tuned BP peak shifts with `noise_pitch`.

**Integration:** plugin compiles (Standalone + VST3 bundle); `pluginval --strictness-level 10 --validate-in-process` still passes with new params.

**Manual (PRD §12.3):** vector muted → raise `noise_level`, sweep `noise_color` 0→1 (hear white→pink→brown); toggle `noise_tuned`, play notes (hear pitched wind); raise `sub_level` (hear sub-octave thickening).

## Task sequence (TDD, one commit each)

1. `SvfFilter` + `test_svf_filter.cpp`.
2. `SubOscillator` + `test_sub_oscillator.cpp`.
3. `NoiseGenerator` — color morph (white/pink/brown) + tests.
4. `NoiseGenerator` — tuned BP + always-on noise filter + tests.
5. `NoiseGenerator` — S&H (built, unrouted) + tests.
6. Params — `sub_*` + `noise_*` in `ParameterLayout.cpp`.
7. Wire sub + noise into voice/processor mixer; build + pluginval + manual check.

## Out of scope (later phases)

- Routing Noise S&H into the mod matrix — Phase 5.
- Main filter / drive / filter ADSR (reuses `SvfFilter`) — Phase 4.
- Pan, unison, glide — Phases 7.
- Custom noise-color visualization UI — Phase 9.
