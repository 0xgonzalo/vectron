# PRD — VEKTOR · Hybrid Vector Synth (JUCE)

> **Codename:** `VEKTOR` *(placeholder — rename it with a global find-replace before you start)*
> **Type:** Polyphonic instrument (synth) · VST3 / AU / Standalone / CLAP
> **For:** implementation with Claude Code
> **Doc version:** 1.1 (vector trajectory included)

---

## 1. Vision

A **hybrid** synthesizer combining a **vector synthesis engine** (X/Y crossfade between 4 oscillators) with a **classic subtractive backbone** (shared filter, drive, envelopes, unison) and a **first-class multi-variable noise generator**.

The goal is to not get stuck in avant-garde sound design: the vector engine brings movement and timbral mutation, but the instrument has to be able to make **powerful, musical synth sounds** (basses, leads, pads, plucks) thanks to the tonal layer and the subtractive backbone. The vector is *one more* modulation source, not the whole instrument.

### Design principles
- **Tonal by default, experimental on demand.** With everything at zero it sounds like a decent analog synth; open up the vector and noise and you head into texture.
- **A single filter and a single VCA at the end** unify everything so it sounds like *one* instrument, not 3 things glued together.
- **Universal modulation** via mod matrix: any source to almost any destination.
- **CPU-friendly:** no allocations on the audio thread, smoothed parameters, anti-aliasing where it matters.

---

## 2. Tech stack

| Item | Decision |
|---|---|
| Framework | **JUCE 8.0.13** (or latest stable 8.x) |
| Language | **C++20** |
| Build | **CMake** (≥ 3.22), no Projucer |
| DSP | `juce_dsp` module (LadderFilter, Oscillator, ProcessorChain, etc.) |
| State/params | `AudioProcessorValueTreeState` (APVTS) |
| Formats | **VST3**, **AU** (macOS), **Standalone**; **CLAP** via [`clap-juce-extensions`](https://github.com/free-audio/clap-juce-extensions) |
| Platforms | macOS (universal arm64+x86_64), Windows x64; Linux optional |
| v1 editor | `juce::GenericAudioProcessorEditor` (DSP first); custom GUI in a later phase |

### External dependencies
- JUCE (git submodule or `FetchContent`).
- `clap-juce-extensions` (submodule) for the CLAP target.
- `pluginval` for QA (not linked, used in CI/manually).

---

## 3. Signal architecture (per voice)

```
                 ┌──────────────── VECTOR ENGINE ────────────────┐
   note on  ──▶  │  OSC A ─┐                                      │
                 │  OSC B ─┤   bilinear XY crossfade  ──▶ vecOut  │
                 │  OSC C ─┤        (pos X,Y mod)                 │
                 │  OSC D ─┘                                      │
                 └───────────────────────────────────────────────┘
                              │
   SUB OSC ───────────────────┤
   NOISE GEN ─────────────────┤
                              ▼
                          [ MIXER ]  (per-source levels)
                              │
                          [ DRIVE / SHAPER ]   (pre-filter by default)
                              │
                          [ FILTER ]  (SVF clean | Ladder Moog-style)
                              │
                          [ VCA ]  ◀── Amp ADSR · Velocity
                              │
                          voice out ──▶ (voice sum) ──▶ MASTER FX ──▶ out
```

**Modulation** (runs in parallel, feeds destinations via the mod matrix):
`LFO1 · LFO2 · Amp ADSR · Filter ADSR · Mod ADSR · Velocity · ModWheel · Aftertouch · KeyTrack · Noise S&H · Random(per-note)`

---

## 4. Voice management

- Polyphony **16 voices** by default (param `poly_voices`, 1–32).
- Implement with `juce::Synthesiser` + a custom `VektorVoice : juce::SynthesiserVoice` class and `VektorSound`.
- **Voice stealing:** oldest / lowest-in-envelope voice. Use `juce::Synthesiser`'s default stealing for v1; refine later.
- **Glide/portamento:** mode `off / legato / always`, time 0–2000 ms (log). In mono, glide between notes; in poly, glide per voice from the last pitch.
- **Mono/poly mode** (`voice_mode`): Poly, Mono, Legato (mono with conditional envelope retrigger).
- Each voice owns its own **vector trajectory playhead** (see 5.2.1), pre-allocated.
- Pitch bend range param (default ±2 st).
- Denormals: `juce::ScopedNoDenormals` in `processBlock`.
- **No allocations** in the render. All per-voice state pre-allocated.

---

## 5. DSP modules (detail)

### 5.1 Vector oscillators (OSC A · B · C · D)

Each of the 4 is identical and configurable. They are the "corners" of the vector square.

- **Waveform** (`oscX_wave`): `Sine | Triangle | Saw | Pulse`.
  - Saw and Pulse with **PolyBLEP anti-aliasing**. Pure sine; naive triangle or PolyBLAMP (optional).
- **Octave** (`oscX_oct`): -3 … +3.
- **Coarse** (`oscX_coarse`): -24 … +24 semitones.
- **Fine** (`oscX_fine`): -100 … +100 cents.
- **Pulse Width** (`oscX_pw`): 0.05 … 0.95 (Pulse only). Modulatable (PWM).
- **Level** (`oscX_level`): 0 … 1 (base gain of the corner, on top of the vector weight).
- **Phase reset on note-on** (`oscX_phaseReset`): bool. If off, free-running.
- **Key tracking:** always follows the note (it's tonal).

> Implementation note: each osc keeps its phase in `[0,1)`. Increment = freq/sampleRate. Freq derived from MIDI note + oct + coarse + fine + pitch mods.

### 5.2 Vector engine (X/Y crossfade)

- Position **X ∈ [-1, +1]**, **Y ∈ [-1, +1]**.
- **Corner mapping** (normalizing u=(X+1)/2, v=(Y+1)/2):
  - `OSC A` = top-left  → weight `gA = (1-u) * v`
  - `OSC B` = top-right → weight `gB = u * v`
  - `OSC C` = bottom-left  → weight `gC = (1-u) * (1-v)`
  - `OSC D` = bottom-right → weight `gD = u * (1-v)`
  - (Weights sum to 1 → bilinear interpolation.)
- **Crossfade mode** (`vector_xfade`): `Linear` (default) | `Equal-Power` (apply `sqrt` to each weight and renormalize) so the center doesn't drop in level.
- **Base position** (`vector_x`, `vector_y`): -1 … +1, what the user sets on the XY pad.
- **Dedicated vector LFOs** (replicate the `slow X / slow Y` from the reference patch): each axis has its own LFO with:
  - `vector_xLfoRate` / `vector_yLfoRate`: 0.01 … 20 Hz (log) + optional tempo sync.
  - `vector_xLfoDepth` / `vector_yLfoDepth`: 0 … 1.
  - `vector_xLfoShape` / `vector_yLfoShape`: Sine | Tri | Saw | Square | S&H.
- **Final position** per axis (unified model, see 5.2.1):
  ```
  finalX = clamp( base_x·(1-traj_depth) + traj_x·traj_depth + vectorLFO_x + mods_x , -1, +1 )
  finalY = clamp( base_y·(1-traj_depth) + traj_y·traj_depth + vectorLFO_y + mods_y , -1, +1 )
  ```
  Smoothed. With `traj_depth = 0` the vector stays at the static position (`base`); with `traj_depth = 1` it follows the pure trajectory. LFOs and the mod matrix always add on top.
- `vecOut = gA·A + gB·B + gC·C + gD·D`.

### 5.2.1 Vector Trajectory (vector envelope / sequencer) ⭐

The feature that sets this synth apart: instead of sitting at a fixed point, the vector position **travels a path drawn/recorded across the XY plane over time**, Prophet VS / Korg Wavestation style. This turns every note into a timbral gesture that evolves on its own — key for pads, drones, and generative textures.

**Data model**
- A **list of points** `P0 … Pn`, up to **16** (configurable max), default 4. Each point:
  - `x` ∈ [-1, +1], `y` ∈ [-1, +1] — position in the plane.
  - `time` — duration to travel **from the previous point to this one** (ms 1–10000, or tempo divisions if synced).
  - `tension` (optional, -1…+1) — curvature of the incoming segment (0 = straight).
- The first point `P0` is the start point at note-on (its `time` is ignored).
- **Point data does NOT go as automatable APVTS params.** It is stored as a `ValueTree` child of the APVTS state (serialized with the preset). Only the macro-controls below are automatable APVTS params. (Same pattern as a step-seq or a wavetable path.)

**Macro-controls (APVTS params)**
- **`traj_mode`:** `Off` | `One-Shot` | `Loop` | `Loop+Sustain`.
  - *Off:* static position (`base`).
  - *One-Shot:* travels `P0→Pn` once on note-on, holds at `Pn`.
  - *Loop:* loops the loop region indefinitely.
  - *Loop+Sustain:* travels to the loop region, loops **while the note is held**, and on release continues to `Pn` (or wherever release dictates). Analogous to an envelope with a loop + tail.
- **`traj_depth`:** 0 … 1 — static↔trajectory blend (see formula in 5.2). Modulatable.
- **`traj_rate`:** 0.25× … 4× — global speed multiplier for all segments.
- **`traj_sync`:** bool — if on, point `time` values are interpreted as tempo divisions and `traj_rate` becomes a base-division selector.
- **`traj_loopStart` / `traj_loopEnd`:** point indices (int) bounding the loop region.
- **`traj_loopDir`:** `Forward` | `Ping-Pong` | `Reverse`.
- **`traj_interp`:** `Linear` (hard corners) | `Smooth` (cosine/cubic, curved path). If per-point `tension` is used, this sets the base mode.
- **`traj_trigger`:** `Per-Note` (each voice runs its own playhead, default) | `Global` (free-running, shared across voices).
- **`traj_retrigger`:** bool — in Per-Note mode, whether it restarts from `P0` on each note-on (on) or latches the global phase (off).

**Playback / DSP**
- Each voice keeps its own **playhead**: `{ segIndex, segPhase, elapsed, dir }`. Pre-allocated, no allocations in the render.
- Per block (control-rate is enough; smooth the output): advance `segPhase` by `time·(1/traj_rate)`/sampleRate, interpolate `(x,y)` between `P[segIndex]` and `P[segIndex+1]` with `traj_interp`/`tension`, handle end-of-segment, looping (incl. ping-pong by flipping `dir`), and sustain.
- The output `(traj_x, traj_y)` feeds the unified model in 5.2.
- **`Global` mode:** a single instrument-level playhead, useful so a whole pad moves in phase.

**Recording (record)** — UX, see §9
- *Record-arm* mode on the XY pad: when armed, dragging the mouse/touch across the plane **captures the trajectory in real time** (position + timing) and **resamples it to N points** (param `traj_recPoints`, 4–16). Lets you "play" a gesture and keep it as a reproducible vector envelope. Optional quantize-to-tempo while recording if `traj_sync` is on.

### 5.3 Sub oscillator

Body and weight, outside the vector.
- **Waveform** (`sub_wave`): Sine | Triangle | Square.
- **Octave** (`sub_oct`): -1 | -2 relative to the note.
- **Level** (`sub_level`): 0 … 1.
- Tracks the note pitch (no detune of its own in v1).

### 5.4 Noise generator (featured module)

It has to be good and variable, not a flat white noise.

- **Color** (`noise_color`): 0 … 1 morphs **White (0) → Pink (0.5) → Brown (1)**.
  - Pink: Paul Kellet filter (one-pole network) or Voss-McCartney.
  - Brown: leaky integrator (`y += (white - y) * coef`), normalized.
  - Implement all three and **crossfade** by `noise_color` (white↔pink in [0,0.5], pink↔brown in [0.5,1]).
- **Tuned noise** (`noise_tuned`: bool): passes the noise through an **SVF in band-pass with high Q** whose cutoff tracks the note pitch → "tuned" noise (wind with a note).
  - `noise_pitch` (-24 … +24 st) and `noise_keytrack` (0 … 100%) control the tracking.
- **Noise filter** (always active, independent of tuned): type `noise_filterType` (HP | BP | LP), `noise_cutoff` (20 Hz … 20 kHz log), `noise_reso` (0 … 1).
- **Level** (`noise_level`): 0 … 1 → into the mixer.
- **Sample & Hold** (mod source, NOT audio): samples the noise value at `noise_sh_rate` (0.1 … 50 Hz, or tempo sync) with `noise_sh_glide` (0 … 1 smoothing). Exposed as **mod source `Noise S&H`** in the matrix. Perfect for tempo-syncable random modulation.

### 5.5 Mixer + Drive

- **Mixer:** sums `vecOut` (with `vector_level`), `sub` (`sub_level`), `noise` (`noise_level`).
- **Drive / Shaper:**
  - `drive_type`: `Tanh (soft)` | `Hard clip` | `Foldback`.
  - `drive_amount`: 0 … 1 (maps to internal pre-gain, e.g. 1×…20×).
  - `drive_trim`: output gain -24 … +6 dB (compensates the level).
  - `drive_position`: `Pre-filter` (default) | `Post-filter`.

### 5.6 Filter

The subtractive heart. Two selectable engines:
- **`filter_type`:** `SVF (clean, TPT/Zavalishin)` | `Ladder (Moog-style)`.
  - For Ladder use **`juce::dsp::LadderFilter`** (has LPF12/LPF24/HPF12/HPF24/BPF12/BPF24 and internal drive).
  - For SVF, TPT state-variable with LP/BP/HP/Notch outputs.
- **`filter_mode`:** LP | BP | HP | Notch.
- **`filter_slope`:** 12 | 24 dB/oct (where the engine allows).
- **`filter_cutoff`:** 20 Hz … 20 kHz (log skew, default ~1 kHz).
- **`filter_reso`:** 0 … 1 (with self-oscillation near the top on Ladder).
- **`filter_drive`:** 0 … 1 (feeds Ladder's internal drive / SVF pre-gain).
- **`filter_keytrack`:** -100 … +100 % (how much cutoff follows the note).
- **`filter_envAmount`:** -1 … +1 (bipolar; Filter ADSR → cutoff).
- Effective cutoff = `cutoff · keytrack · 2^(filterEnv·amount·octaves) + mods`.

### 5.7 Amp / VCA

- VCA at the end of the voice, gain = `AmpADSR · velocity_curve · vca_level`.
- **`amp_velSens`:** 0 … 1 (how much velocity affects volume).
- **`vca_level`:** voice gain, -inf … 0 dB.

---

## 6. Modulation

### 6.1 Envelopes (×3 ADSR)
- **Amp Env** — hardwired to the VCA (always).
- **Filter Env** — default → cutoff, but also routable in the matrix.
- **Mod Env** — free, matrix-only.
- Each: `attack` (0–10 s, log), `decay` (0–10 s), `sustain` (0–1), `release` (0–15 s), `velToAmount` (0–1).
- Exponential segment curves (not linear).

### 6.2 LFOs (×2 global/per-voice)
Each LFO:
- **`lfoN_shape`:** Sine | Triangle | Saw↑ | Saw↓ | Square | S&H | Random (smooth).
- **`lfoN_rate`:** 0.01 … 40 Hz (log).
- **`lfoN_sync`:** bool → tempo divisions (`1/1 … 1/32`, incl. dotted/triplet).
- **`lfoN_phase`:** 0 … 360°.
- **`lfoN_fadeIn`:** 0 … 5 s.
- **`lfoN_polarity`:** Bipolar | Unipolar.
- **`lfoN_mode`:** Poly (retrigger per voice) | Global (free-running, shared).

### 6.3 Mod Matrix (8 slots)
Each slot: **`source` → `destination`**, with **`amount` (-1 … +1)** and `enable` (bool).

**Sources:**
`LFO1 · LFO2 · AmpEnv · FilterEnv · ModEnv · Velocity · ModWheel · Aftertouch (channel) · KeyTrack · Noise S&H · Random(per-note)`

**Destinations:**
`VectorX · VectorY · OscA/B/C/D Pitch · OscA/B/C/D PW · OscA/B/C/D Level · Sub Level · Noise Level · Noise Color · Noise Cutoff · Filter Cutoff · Filter Reso · Drive Amount · Amp Level · LFO1 Rate · LFO2 Rate · Pan`

> Implementation: a fixed array of 8 `ModSlot{ sourceId, destId, amount, enabled }`. Per block, evaluate the sources (control-rate or sample-rate depending on the destination) and accumulate into the destinations before applying.

### 6.4 Unison (global)
- **`unison_voices`:** 1 … 7.
- **`unison_detune`:** 0 … 100 cents (pitch spread between copies).
- **`unison_spread`:** 0 … 1 (stereo panorama).
- **`unison_blend`:** 0 … 1 (center voice vs. sides balance).
- Implemented by stacking N detuned sub-voices per note. Watch the total voice count vs. polyphony/CPU.

---

## 7. Master FX (post voice-sum)

A light chain for lushness without bloating scope. **v1.1 optional**, but spec the nodes:
1. **Chorus** — rate, depth, mix (use `juce::dsp::Chorus`).
2. **Delay** — time (tempo sync), feedback, mix, ping-pong toggle.
3. **Reverb** — `juce::dsp::Reverb` or a simple FDN algorithm: size, damp, width, mix.
- Master: **`master_volume`** (-inf … +6 dB), **`master_tune`** (A4 = 415 … 466 Hz, default 440).

---

## 8. Parameter table (APVTS) — summary

> ID convention: `snake_case`. All floats unless marked. All with `SmoothedValue` where applicable. This table is the contract; the exact ranges above are authoritative.

| Group | IDs (examples) | Type | Notes |
|---|---|---|---|
| Vector | `vector_x`, `vector_y`, `vector_xfade`, `vector_level`, `vector_{x,y}Lfo{Rate,Depth,Shape}` | float/choice | XY + dedicated LFOs |
| Trajectory | `traj_mode`, `traj_depth`, `traj_rate`, `traj_sync`, `traj_loopStart`, `traj_loopEnd`, `traj_loopDir`, `traj_interp`, `traj_trigger`, `traj_retrigger`, `traj_recPoints` | mixed | macro-controls; **points live in a state ValueTree, not as params** |
| Osc A–D | `oscA_wave`…`oscD_*`: `wave,oct,coarse,fine,pw,level,phaseReset` | choice/float/bool | ×4 identical |
| Sub | `sub_wave`, `sub_oct`, `sub_level` | choice/float | |
| Noise | `noise_color`, `noise_tuned`, `noise_pitch`, `noise_keytrack`, `noise_filterType`, `noise_cutoff`, `noise_reso`, `noise_level`, `noise_sh_rate`, `noise_sh_glide` | mixed | featured module |
| Mixer/Drive | `drive_type`, `drive_amount`, `drive_trim`, `drive_position` | choice/float | |
| Filter | `filter_type`, `filter_mode`, `filter_slope`, `filter_cutoff`, `filter_reso`, `filter_drive`, `filter_keytrack`, `filter_envAmount` | mixed | SVF / Ladder |
| Amp | `amp_velSens`, `vca_level` | float | |
| Env ×3 | `{amp,filt,mod}_{attack,decay,sustain,release,velAmt}` | float | |
| LFO ×2 | `lfo1_*`, `lfo2_*`: `shape,rate,sync,phase,fadeIn,polarity,mode` | mixed | |
| Mod Matrix | `mod{1..8}_{src,dst,amt,en}` | choice/float/bool | 8 slots |
| Unison | `unison_{voices,detune,spread,blend}` | int/float | |
| Voice | `poly_voices`, `voice_mode`, `glide_mode`, `glide_time`, `bend_range` | int/choice/float | |
| FX | `chorus_*`, `delay_*`, `reverb_*` | mixed | v1.1 |
| Master | `master_volume`, `master_tune` | float | |

---

## 9. GUI

### Phase 1 (DSP first)
- Use **`juce::GenericAudioProcessorEditor`**. Zero time on UI until the engine makes sound.

### Phase 2 (custom GUI)
Panel layout:
- **XY Vector Pad** (large, central) — the star component. It has three jobs:
  - **Position:** drag the static position (`base`), with an animated trail of LFO/mods/trajectory movement in real time.
  - **Edit trajectory:** shows points `P0…Pn` as nodes connected by the path; drag nodes, add/remove with click/alt-click, and set each segment's `time` (slider on the node or a side table). Visually mark the loop region (`loopStart→loopEnd`) and the direction.
  - **Record (record-arm):** arm button → drag a gesture across the pad → captures the trajectory and resamples it to `traj_recPoints`. Recording indicator + countdown if tempo quantize is on.
  - Animated playhead visible (a dot traveling the path) while a note sounds, using JUCE 8's animation framework.
- **OSC A–D** (4 compact strips).
- **Sub · Noise** (own panel, with noise color visualization).
- **Filter** (cutoff/reso prominent).
- **Envelopes ×3 · LFOs ×2** (with curve drawing).
- **Mod Matrix** (8-row grid).
- **Unison · Glide · Master · FX**.

Suggested aesthetic (your call): **dark / digital brutalism**, high contrast, mono typography for values, minimal accents. Animate the vector trail using **JUCE 8's new animation framework**.

---

## 10. Project structure

```
vektor/
├── CMakeLists.txt
├── libs/
│   ├── JUCE/                  (submodule)
│   └── clap-juce-extensions/  (submodule)
├── source/
│   ├── PluginProcessor.{h,cpp}
│   ├── PluginEditor.{h,cpp}
│   ├── params/
│   │   └── ParameterLayout.{h,cpp}      # builds the APVTS layout
│   ├── dsp/
│   │   ├── VektorVoice.{h,cpp}
│   │   ├── VektorSound.h
│   │   ├── osc/PolyBlepOscillator.{h,cpp}
│   │   ├── osc/VectorEngine.{h,cpp}
│   │   ├── osc/VectorTrajectory.{h,cpp}  # point model + per-voice playhead
│   │   ├── osc/SubOscillator.{h,cpp}
│   │   ├── noise/NoiseGenerator.{h,cpp}
│   │   ├── filter/SvfFilter.{h,cpp}
│   │   ├── filter/FilterStage.{h,cpp}   # wraps SVF + Ladder
│   │   ├── mod/Envelope.{h,cpp}
│   │   ├── mod/Lfo.{h,cpp}
│   │   └── mod/ModMatrix.{h,cpp}
│   └── gui/   (phase 2)
├── presets/
└── tests/
```

---

## 11. Performance & quality

- **No allocations / locks** in `processBlock` or in the voice render.
- `SmoothedValue<float>` on all audible continuous params (cutoff, levels, vector pos).
- **Optional** oversampling (param `oversample`: off/2×) for aggressive drive/filter — `juce::dsp::Oversampling`. Default off.
- Denormal handling (`ScopedNoDenormals`).
- Rough target: ≲ 1% CPU per voice on modern hardware; the plugin must pass **`pluginval`** at strictness 10.

---

## 12. Implementation phases (order for Claude Code)

1. **Scaffold + sound.** CMake + empty VST3/Standalone plugin, 1 VA osc (saw) + Amp ADSR. Make it play a note. ✅ criterion: plays monophonic, in tune.
2. **Vector engine.** 4 osc + bilinear XY crossfade + `vector_x/y` from the generic editor. ✅ moving XY changes the timbre.
3. **Sub + Noise.** Sub osc and full noise module (color, tuned, filter, S&H as mod source). ✅ audible variable noise.
4. **Filter + Drive.** FilterStage (SVF + Ladder), Filter ADSR, drive. ✅ cutoff sweeps with env.
5. **Modulation.** LFOs ×2 + 8-slot Mod Matrix + Mod Env. ✅ routing LFO→VectorX and env→cutoff works.
6. **Vector Trajectory (engine).** Point model in a ValueTree + per-voice playhead + modes (One-Shot/Loop/Loop+Sustain/loopDir/interp) + `traj_depth` blend. Edit points by hand in the state for now. ✅ a note travels the XY path and loops.
7. **Voices.** Polyphony, voice stealing, glide, mono/legato, unison. ✅ chords + fat unison.
8. **Master FX.** Chorus/Delay/Reverb. ✅ lush pads.
9. **Custom GUI.** Panels + **editable/recordable XY pad** (nodes, loop region, record-arm, animated playhead).
10. **Presets + QA.** 10–15 factory presets (bass, lead, pad, pluck, drone, fx — several leaning on the trajectory), `pluginval`, CPU profiling.

> After each phase: compile, load into a host (AudioPluginHost / DAW), and validate the criterion before moving on.

---

## 13. Testing / QA

- **`pluginval`** (high strictness) at every milestone with new DSP.
- Unit tests on isolated DSP blocks (osc tuning, vector weights sum to 1, noise color).
- Bypass null test; click checks on note-on/off (envelopes).
- Aliasing sanity: sweep a high freq and inspect the spectrum.

---

## 14. Out of scope (v1) / future

- Importable wavetables in the vector corners (v2).
- Wave-sequencing (per-step waveform sequence, Wavestation style — distinct from the vector trajectory, which is already in v1).
- MPE / per-note expression.
- Microtuning (Scala/MTS-ESP).
- FX modulation from the matrix.

---

## 15. Quick glossary

- **PolyBLEP:** cheap anti-aliasing technique for saw/pulse (corrects the jump at the discontinuity).
- **SVF (TPT):** topology-preserving state-variable filter (Zavalishin), stable under variable sample-rate and fast modulation.
- **Bilinear crossfade:** 2D interpolation of 4 sources by (X,Y), weights sum to 1.
- **Vector trajectory / vector envelope:** a path of points across the XY plane that the vector position travels over time (Prophet VS / Wavestation). Each note fires its own playhead.
- **APVTS:** `AudioProcessorValueTreeState`, JUCE's params/state system.
