# Vectron Phase 5 — Modulation (Design Spec)

**Status:** Approved design; ready to expand into a TDD implementation plan.
**Date:** 2026-07-23
**Predecessor:** Phase 4 (Filter + Drive) — complete, tests pass.

## Goal

Add the modulation system: two full-featured **LFOs** (poly/global, tempo-syncable), a free **Mod Env**, an 8-slot **Mod Matrix** (11 sources × 25 destinations), and the **exponential-segment ADSR migration** deferred from Phase 4 (all three envelopes move to a custom `AdsrEnvelope` at once).

**Acceptance criterion (PRD §12.5):** routing LFO→VectorX and env→cutoff through the matrix audibly works.

## Global constraints (inherited)

- **Framework:** JUCE 8.0.13. **Language:** C++20. **Build:** CMake ≥ 3.22.
- **JUCE-free DSP:** leaf DSP classes (`ModLfo`, `AdsrEnvelope`, `ModMatrix`) include **no** JUCE headers — unit-tested by `VectronTests` (Catch2 only).
- **Real-time safety:** no allocations/locks in `processBlock` or voice render.
- **Param IDs:** `snake_case` per PRD §8. Choice-param option order is load-bearing.

## Design decisions (locked)

1. **Per-sample matrix evaluation.** All 8 slots evaluated every sample inside the voice render (sources are cheap scalars; precedent: per-sample cutoff since Phase 4). No sub-block control-rate machinery in v1.
2. **Exponential `AdsrEnvelope` replaces `juce::ADSR`** for Amp + Filter, and provides the new Mod Env (honors Phase 4 locked decision #1). One-pole exponential segments (EarLevel/Redmon-style overshoot targets). A fresh Phase 5 patch will not null-test against Phase 4 — accepted; the PRD mandates exponential curves.
3. **Tempo sync adds a `lfoN_syncDiv` choice param** (PRD §8 lists `lfoN_sync` only; a division selector is required to be usable). Divisions: `1/1, 1/2., 1/2, 1/2T, 1/4., 1/4, 1/4T, 1/8., 1/8, 1/8T, 1/16., 1/16, 1/16T, 1/32., 1/32, 1/32T`. Host BPM from the playhead, fallback 120.
4. **Global LFO mode = shared absolute phase.** The processor advances one master phase accumulator per LFO (double, cycles); each voice in Global mode derives its LFO phase as `master + phaseOffset + startSample·inc`, so chunked `renderNextBlock` calls stay coherent. Poly mode retriggers at note-on from `lfoN_phase`.
5. **Hash-based S&H / Random-smooth LFO shapes.** The random value for cycle *k* is `rand(hash(k))` on the unwrapped cycle index — deterministic, phase-jump-safe, and identical across voices in Global mode. Random (smooth) interpolates `rand(hash(k)) → rand(hash(k+1))` with cosine easing. In **Poly** mode the seed is re-mixed with a per-note nonce at note-on so S&H/Random shapes don't replay the identical sequence every note; the fixed seed is Global-mode-only (review finding, Phase 5).
6. **LFO fade-in is per-note in both modes** (depth ramp from note-on); only *phase* retrigger is Poly-mode-only.
7. **Destination scaling (full-scale at amount ±1):** pitch ±12 st · PW ±0.45 · levels/color/reso/drive ±1 additive then clamp to range · Vector X/Y ±1 additive then clamp ±1 · filter & noise cutoff ±5 octaves (matches the Phase 4 env convention) · LFO rate ±3 octaves · Amp Level gain `clamp(1+mod, 0, 2)` · Pan ±1 full L↔R equal-power.
8. **LFO rate cross-mod uses the previous sample's matrix output** (one-sample feedback delay — standard, documented).
9. **`amp_velSens` defaults to 1.0** (current behavior is full velocity sensitivity; default preserves it). Formula: `velGain = 1 − sens + sens·velocity`. Mod Env `mod_velAmt` scales the env the same way before it enters the matrix.
10. **Sources have no "None" option** — a slot defaults to `enabled = false` (LFO1→VectorX, amount 0). 11 sources / 25 destinations exactly as PRD §6.3 (2 vector + 12 osc + 2 sub/noise levels + 2 noise tone + 2 filter + 1 drive + 1 amp + 2 LFO rate + 1 pan).
11. **Aftertouch = channel pressure** (poly aftertouch ignored in v1, per PRD "Aftertouch (channel)").
12. **KeyTrack source = `(note − 60) / 60`**, clamped to ±1 (MIDI 0 ≈ −1, 120 ≈ +1, middle C = 0).
13. **Pan applies only when the output has ≥ 2 channels**; equal-power sin/cos law. Unmodulated pan is exactly center and nulls the mono path.
14. **ModWheel/Aftertouch smoothed** (~5 ms one-pole at voice level) to kill CC zipper.
15. **Existing per-axis vector LFOs (Phase 2) stay unchanged** — matrix Vector X/Y offsets add on top: `clamp(base + axisLfo + matrix, −1, 1)`.
16. **Unison (§6.4) stays in Phase 7** per PRD §12.
17. **LFO-rate cross-mod is Poly-mode-only.** A Global-mode LFO's phase is pinned to the processor's master accumulator (advanced at the base rate), so per-voice rate mod would be snapped back at every block boundary — the voice therefore ignores `LFO N Rate` matrix routings for LFOs in Global mode (review finding, Phase 5).

## Architecture

### New files

| File | Purpose | Depends on |
|---|---|---|
| `source/dsp/mod/AdsrEnvelope.h` | Exponential-segment ADSR. API mirrors `juce::ADSR` (`setSampleRate`, `setParameters{a,d,s,r}`, `noteOn`, `noteOff`, `reset`, `getNextSample`, `isActive`) plus `getCurrentValue()` for mod-source reads. Header-only, JUCE-free. | `<cmath>` |
| `source/dsp/mod/ModLfo.h` | 7-shape LFO (Sine, Triangle, Saw↑, Saw↓, Square, S&H, Random-smooth) with phase offset, fade-in, polarity, absolute-phase API (`setAbsolutePhase(double)`), hash-based randoms. Header-only, JUCE-free. | `<cmath>`, `<cstdint>` |
| `source/dsp/mod/ModMatrix.h` | `Source`/`Dest` enums, `Slot{source,dest,amount,enabled}`, and `evaluate(slots, sourceValues[], destOffsets[])` — zero + accumulate, pure. Header-only, JUCE-free. | none |
| `tests/test_adsr_envelope.cpp` | Envelope unit tests. | Catch2 |
| `tests/test_mod_lfo.cpp` | LFO unit tests. | Catch2 |
| `tests/test_mod_matrix.cpp` | Matrix unit tests. | Catch2 |

### Modified files

- `source/dsp/filter/FilterMath.h` — `effectiveCutoffHz(..., float modOct = 0)` extra additive-octaves term from the matrix.
- `source/dsp/osc/VectorEngine.h/.cpp` — per-osc `setPitchModSemis(idx, semis)` (folds into `updateFrequency`, recomputed only when changed > 1e-4 st) and per-osc PW/level already settable per sample.
- `source/params/ParameterLayout.cpp` — ~54 new params (below).
- `source/dsp/VectronVoice.h/.cpp` — swap `juce::ADSR`→`AdsrEnvelope` (amp, filt), add mod env, 2× `ModLfo`, matrix slots + per-sample evaluation/application, mod-wheel/aftertouch state, per-note random, pan.
- `source/PluginProcessor.h/.cpp` — cache new param pointers, read BPM, advance 2 master LFO phases, push everything per block.
- `tests/CMakeLists.txt` — add the three new test files.

### New parameters

| Group | IDs | Type / range |
|---|---|---|
| LFO ×2 | `lfo{1,2}_shape` (choice ×7), `_rate` (0.01–40 Hz log, default 1), `_sync` (bool), `_syncDiv` (choice ×16, default 1/4), `_phase` (0–360°), `_fadeIn` (0–5 s), `_polarity` (Bipolar/Unipolar), `_mode` (Poly/Global) | 16 params |
| Mod Env | `mod_attack` (0–10 s log), `mod_decay` (0–10 s), `mod_sustain` (0–1), `mod_release` (0–15 s), `mod_velAmt` (0–1) | 5 params |
| Amp | `amp_velSens` (0–1, default 1) | 1 param |
| Matrix ×8 | `mod{1..8}_src` (choice ×11), `_dst` (choice ×25), `_amt` (−1…+1, default 0), `_en` (bool, default off) | 32 params |

Source choice order: `LFO1, LFO2, AmpEnv, FilterEnv, ModEnv, Velocity, ModWheel, Aftertouch, KeyTrack, NoiseSH, RandomNote`.
Dest choice order (25): `VectorX, VectorY, OscA Pitch, OscB Pitch, OscC Pitch, OscD Pitch, OscA PW, OscB PW, OscC PW, OscD PW, OscA Level, OscB Level, OscC Level, OscD Level, Sub Level, Noise Level, Noise Color, Noise Cutoff, Filter Cutoff, Filter Reso, Drive Amount, Amp Level, LFO1 Rate, LFO2 Rate, Pan`. **The enums in `ModMatrix.h` are the contract; the param choice arrays are built from matching name tables with `static_assert` on the counts.**

### Per-sample voice render order

```
1. advance envelopes once → ampEnvVal, filtEnvVal(vel-scaled), modEnvVal(vel-scaled)
2. LFO rates = baseRate · 2^(prevOffset[LfoNRate]·3); lfo.processSample → lfo1, lfo2
3. sources[] = { lfo1, lfo2, ampEnvVal, filtEnvVal, modEnvVal, velocity,
                 modWheel(smoothed), aftertouch(smoothed), keytrack, noiseSH(prev), randNote }
4. ModMatrix::evaluate → destOffsets[]
5. apply: engine pitch/PW/level mods, vector position (base + axisLfo + matrix, clamped),
   sub/noise levels, noise color/cutoff, filter cutoff (env + matrix octaves), reso,
   drive amount, then render osc→drive→filter→drive chain as in Phase 4
6. VCA: s · ampEnvVal · velGain · ampLevelMod · 0.3 ; equal-power pan to L/R
```

## DSP details

### AdsrEnvelope

- One-pole toward overshoot targets: attack aims at `1/(1−exp(−1/…))`-style ratio 0.3 above 1 (clipped at 1.0); decay/release aim below target with ratio 1e-4 (fast-settling exponential tails).
- `noteOn` retriggers from the **current** output (no click); `noteOff` releases from current; output < 1e-4 in Release → inactive, output 0.
- Segment times map knob seconds to the time the segment takes to effectively complete (±10 %).

### ModLfo

- Keeps unwrapped `double phase` (cycles). `value(phase)` per shape; S&H/Random via `hash(uint64(floor(phase)) ^ seed)`.
- `retrigger()` sets phase = phaseOffset (cycles) and restarts fade-in; `startFadeIn()` alone for Global-mode note-ons; `setAbsolutePhase(double)` for Global sync.
- Output × fade (linear 0→1 over fadeIn s) → bipolar; Unipolar maps to `0.5·(v+1)`.

### ModMatrix

- `kNumSources = 11`, `kNumDests = 25` (enum is the single source of truth; param choice arrays are built from matching name tables with `static_assert` on counts).
- `evaluate` zeroes `destOffsets`, then for each enabled slot: `destOffsets[dst] += sourceValues[src] · amount`. No clamping here — clamping happens at application per decision 7.

## Testing

- **AdsrEnvelope:** reaches ≥ 0.99 within attack time; exponential concavity (attack midpoint > 0.5 of peak? — assert curve is convex-up for decay: value at t/2 < linear midpoint); sustain holds; release under 1e-3 within release time ×1.5; retrigger continuity (max per-sample jump bounded); zero-length segments safe; isActive lifecycle.
- **ModLfo:** shape values at known phases (sine 0/0.25/0.5, saw↑ endpoints, square edges); phase-offset correctness; fade-in duration; unipolar ∈ [0,1]; S&H constant within a cycle and deterministic for a given cycle index; Random-smooth per-sample delta bounded; `setAbsolutePhase` reproduces identical output streams across two instances (Global-mode invariant).
- **ModMatrix:** disabled slots contribute nothing; amount scaling; two slots to one dest accumulate; all-enum coverage smoke test.
- **FilterMath:** `modOct` adds to the exponent and clamps at 20 Hz/20 kHz.
- **Integration (manual):** default patch silent-matrix; LFO1→VectorX slot audibly sweeps timbre; ModEnv→FilterCutoff sweeps; pluginval strictness 10.

## Out of scope (deferred)

- Unison (§6.4) → Phase 7. Trajectory (§12.6) → Phase 6. Poly aftertouch. Sub-block control-rate optimization (revisit if CPU profiling demands).
