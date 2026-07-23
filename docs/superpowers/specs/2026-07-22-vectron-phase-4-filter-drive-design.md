# Vectron Phase 4 — Filter + Drive (Design Spec)

**Status:** Approved design; ready to expand into a TDD implementation plan.
**Date:** 2026-07-22
**Predecessor:** Phase 3 (Sub + Noise) — complete, tests pass, pluginval strictness 10 pass.

## Goal

Add the subtractive heart of the synth: a per-voice **filter stage** with two selectable engines (clean TPT SVF and Moog-style Ladder), a **drive/shaper** in the mixer stage (pre- or post-filter), and a **Filter ADSR** with bipolar env amount and keytracking driving the cutoff.

**Acceptance criterion (PRD §12.4):** Filter ADSR sweeps cutoff; both filter engines and drive types audibly work.

## Global constraints (inherited)

- **Framework:** JUCE 8.0.13. **Language:** C++20. **Build:** CMake ≥ 3.22, no Projucer.
- **JUCE-free DSP:** leaf DSP classes (`DriveShaper`, `SvfCascade`, cutoff math) include **no** JUCE headers — unit-tested by `VectronTests` (Catch2 only).
- **Real-time safety:** no allocations/locks in `processBlock` or voice render.
- **Param IDs:** `snake_case`, exactly as in PRD §8. Choice-param option order is load-bearing.
- **Smoothing:** `SmoothedValue<float>` on audible continuous params (cutoff, reso, drive amount, trim).

## Design decisions (locked during brainstorming)

1. **Filter envelope uses `juce::ADSR`** (linear segments), matching the Phase 1 amp envelope. The PRD's exponential-curve custom `Envelope` class is deferred to Phase 5 (when the Mod Env arrives); all three envelopes migrate at once then.
2. **Ladder + Notch falls back to Ladder BP** at the chosen slope (juce::dsp::LadderFilter has no notch). The Phase 9 GUI can grey out Notch when Ladder is selected. SVF gets true Notch at both slopes via cascading.
3. **Architecture: thin JUCE-coupled `FilterStage` over JUCE-free tested cores** (`DriveShaper`, `SvfCascade`, cutoff math), consistent with the Phase 1–3 pattern.
4. **Env depth locked at ±5 octaves** at `filter_envAmount = ±1`.
5. **Drive is exact-identity at amount 0** so a fresh patch nulls against Phase 3 (tanh at gain 1 is never fully clean otherwise).
6. **`filter_cutoff` defaults to 1 kHz per PRD** ("default ~1 kHz"), accepting that a fresh patch sounds darker than Phase 3. PRD is the contract.

## Architecture

### New files

| File | Purpose | Depends on |
|---|---|---|
| `source/dsp/drive/DriveShaper.h` | Tanh / Hard clip / Foldback waveshaper with pre-gain (1×–20×) and output trim. Header-only, JUCE-free. | `<cmath>`, `<algorithm>` |
| `source/dsp/filter/SvfCascade.h` | 12/24 dB SVF: one or two `SvfFilter`s in series, LP/BP/HP/Notch, plus tanh pre-saturation for `filter_drive`. Header-only, JUCE-free. | `SvfFilter.h` |
| `source/dsp/filter/FilterMath.h` | `effectiveCutoffHz(...)` free function: keytrack + envelope cutoff math, clamped. Header-only, JUCE-free. | `<cmath>`, `<algorithm>` |
| `source/dsp/filter/FilterStage.h` / `.cpp` | JUCE-coupled engine switch: owns `SvfCascade` + `juce::dsp::LadderFilter<float>`; (mode, slope) → engine mode mapping incl. Notch→BP fallback; reset on note start and engine switch. | `SvfCascade.h`, `juce_dsp` |
| `tests/test_drive_shaper.cpp` | DriveShaper unit tests. | Catch2 |
| `tests/test_svf_cascade.cpp` | SvfCascade unit tests. | Catch2 |
| `tests/test_filter_math.cpp` | Cutoff math unit tests. | Catch2 |

### Modified files

- `source/params/ParameterLayout.cpp` — add all Phase 4 params.
- `source/dsp/VectronVoice.h` / `.cpp` — add `FilterStage`, `DriveShaper`, filter `juce::ADSR`; extend `VectronVoiceParams`; insert drive/filter between mixer sum and VCA.
- `source/PluginProcessor.cpp` — read Phase 4 params, push per block.
- `tests/CMakeLists.txt` — add the three new test files.

### Signal flow (per voice, per sample)

```
mix = vec·vecLvl + sub·subLvl + noise
  → [DriveShaper if drive_position == Pre]
  → FilterStage (SVF cascade | Ladder, modulated cutoff)
  → [DriveShaper if drive_position == Post]
  → × ampEnv · velocity · 0.3
```

## DSP details

### DriveShaper (JUCE-free)

- API: `setType(Tanh|Hard|Foldback)`, `setAmount(float 0..1)`, `setTrimDb(float -24..+6)`, `float processSample(float x)`.
- Pre-gain `g = 1 + 19·amount` (1×–20×).
- **Tanh:** `std::tanh(g·x)`. **Hard:** `clamp(g·x, -1, 1)`. **Foldback:** triangle-fold of `g·x` into [-1, 1].
- Output multiplied by `trimGain = 10^(trimDb/20)`.
- **Amount == 0 → exact identity** (input passed through untouched, trim still applies). Smoothing of amount/trim happens at the voice level (the shaper itself is stateless).

### SvfCascade (JUCE-free)

- API: `setSampleRate`, `setMode(LP|BP|HP|Notch)`, `setSlope(12|24)`, `setCutoff(hz)`, `setResonance(0..1)`, `setDrive(0..1)`, `reset()`, `float processSample(float)`.
- 12 dB = stage 1 only; 24 dB = both stages in series with identical cutoff/reso/mode (incl. Notch×2).
- `filter_drive` on the SVF path: input pre-saturation `tanh((1 + 3·drive)·x)`, **bypassed (identity) at drive == 0** to keep the clean engine clean.

### FilterStage (JUCE-coupled)

- API: `prepare(sampleRate)`, `setEngine(SVF|Ladder)`, `setMode`, `setSlope`, `setCutoff(hz)` (already-modulated effective value, called per sample), `setResonance`, `setDrive`, `reset()`, `float processSample(float)`.
- Ladder mapping: (LP,12)→LPF12, (LP,24)→LPF24, (BP,12)→BPF12, (BP,24)→BPF24, (HP,12)→HPF12, (HP,24)→HPF24, **(Notch,·)→BPF12/24 fallback**.
- Ladder `filter_drive` mapping: `setDrive(1 + 9·drive)` (1–10).
- Ladder resonance: `setResonance(filter_reso)` directly (0–1, self-oscillates near top).
- `reset()` clears both engines; called from `startNote` and when the engine selection changes mid-note.
- Ladder is prepared with a mono, block-size-1 `ProcessSpec` and driven through its per-sample API (`processSample` + `updateSmoothers()` each sample).

### Cutoff modulation (FilterMath.h, JUCE-free)

```
effectiveCutoffHz(baseHz, midiNote, keytrackPct, env, envAmount)
  = clamp(baseHz · 2^((keytrackPct/100)·(midiNote − 60)/12)
                 · 2^(env · envAmount · 5),
          20, 20000)
```

- Keytrack reference note = MIDI 60; ±100 % → cutoff follows the keyboard fully up/down.
- `env` = Filter ADSR output (0..1) scaled by velocity: `envScale = 1 − velAmt + velAmt·velocity`, i.e. `env = filtAdsr · envScale`.
- Env depth: ±5 octaves at `envAmount = ±1`.

### Voice integration

- Filter ADSR: second `juce::ADSR` (`filtAdsr`) in `VectronVoice`; params pushed per block via `setFiltAdsr(...)` like the amp env; `noteOn`/`noteOff` alongside the amp env; velocity captured in `startNote` for `filt_velAmt`.
- Per-sample in `renderNextBlock`: compute filter env sample → `effectiveCutoffHz(...)` with the smoothed base cutoff → `filterStage.setCutoff(...)` → process the mixed sample through drive/filter per `drive_position`.
- SVF per-sample `setCutoff` recomputes `tan()` once per sample — within the ≲1 % CPU/voice budget. Ladder's internal smoothing handles per-sample target updates.

### Parameters (APVTS — exact PRD §8 IDs)

| ID | Type | Range / options | Default |
|---|---|---|---|
| `filter_type` | choice | SVF, Ladder | SVF |
| `filter_mode` | choice | LP, BP, HP, Notch | LP |
| `filter_slope` | choice | 12, 24 | 24 |
| `filter_cutoff` | float | 20 … 20000 Hz, log | 1000 |
| `filter_reso` | float | 0 … 1 | 0 |
| `filter_drive` | float | 0 … 1 | 0 |
| `filter_keytrack` | float | −100 … +100 % | 0 |
| `filter_envAmount` | float | −1 … +1 | 0 |
| `drive_type` | choice | Tanh, Hard clip, Foldback | Tanh |
| `drive_amount` | float | 0 … 1 | 0 |
| `drive_trim` | float | −24 … +6 dB | 0 |
| `drive_position` | choice | Pre-filter, Post-filter | Pre-filter |
| `filt_attack` | float | 0 … 10 s, log | 0.005 |
| `filt_decay` | float | 0 … 10 s, log | 0.2 |
| `filt_sustain` | float | 0 … 1 | 0.8 |
| `filt_release` | float | 0 … 15 s, log | 0.3 |
| `filt_velAmt` | float | 0 … 1 | 0 |

Defaults keep the phase additive: `filter_envAmount`/`drive_amount` = 0 and mode LP mean the only audible change on a fresh patch is the 1 kHz default cutoff (PRD-mandated).

## Testing

**Unit (Catch2, JUCE-free):**
- `test_drive_shaper.cpp`: amount-0 exact identity; tanh and hard clip monotonic + bounded to [−1, 1] (pre-trim); foldback bounded and actually folds (non-monotonic beyond the fold point); trim dB math (±dB → gain).
- `test_svf_cascade.cpp`: 24 dB attenuates more than 12 dB at 4×fc (LP); cascaded Notch strongly attenuates at fc; bounded output under per-sample cutoff sweeps; drive-0 path identical to plain SvfFilter.
- `test_filter_math.cpp`: keytrack 0 % → base unchanged; +100 % at note 72 → 2× base; −100 % at note 72 → ½ base; envAmount +1 with env 1 → 32× base (5 octaves); bipolar negative direction; clamps at 20 Hz / 20 kHz.

**Integration:** plugin compiles (Standalone + VST3); `pluginval --strictness-level 10 --validate-in-process` passes with new params.

**Manual (PRD §12.4):** raise `filter_envAmount` + short decay → hear the cutoff sweep per note; switch SVF↔Ladder (character change, Ladder self-osc near reso 1); sweep `drive_amount` on each of Tanh/Hard/Foldback; flip `drive_position` pre↔post with high reso (hear the difference).

## Task sequence (TDD, one commit each)

1. `DriveShaper` + `test_drive_shaper.cpp`.
2. `SvfCascade` + `test_svf_cascade.cpp`.
3. `FilterMath` (effective cutoff) + `test_filter_math.cpp`.
4. `FilterStage` (JUCE-coupled engine switch; compile-verified).
5. Params — `filter_*`, `drive_*`, `filt_*` in `ParameterLayout.cpp`.
6. Wire drive + filter + filter ADSR into voice/processor; build + pluginval + manual check.

## Out of scope (later phases)

- Custom exponential `Envelope` class (all 3 envelopes migrate) — Phase 5.
- Mod-matrix routes into cutoff/reso/drive (`+ mods` term) — Phase 5.
- Filter GUI, greying out Notch on Ladder — Phase 9.
