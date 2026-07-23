# Vectron Phase 6 — Vector Trajectory Engine (Design Spec)

**Status:** Approved design; ready to expand into a TDD implementation plan.
**Date:** 2026-07-23
**Predecessor:** Phase 5 (Modulation) — complete, merged, 68 tests pass.

## Goal

Add the **Vector Trajectory engine** (PRD §5.2.1, §12.6): the vector position travels a path of points across the XY plane over time, Prophet VS / Wavestation style. Point model in a `ValueTree`, per-voice playhead, modes (Off / One-Shot / Loop / Loop+Sustain), loop direction, interpolation, tempo sync, Per-Note/Global trigger, retrigger latch, and the `traj_depth` static↔trajectory blend. Points are edited by hand in the state for now (recording UX is GUI-phase).

**Acceptance criterion (PRD §12.6):** a note travels the XY path and loops — audibly verified by the smoke harness.

## Scope

**In:** full macro-param set — `traj_mode`, `traj_depth`, `traj_rate`, `traj_sync`, `traj_loopStart`, `traj_loopEnd`, `traj_loopDir`, `traj_interp`, `traj_trigger`, `traj_retrigger`, plus `traj_recPoints` as a dormant param (used by GUI recording later, added now so the layout doesn't churn). One new mod-matrix destination `DstTrajDepth`.

**Out:** XY-pad editing/recording UX (§9, GUI phase), quantize-to-tempo recording, unison (Phase 7).

## Global constraints (inherited)

- **Framework:** JUCE 8.0.13. **Language:** C++20. **Build:** CMake ≥ 3.22.
- **JUCE-free DSP:** the new leaf classes (`TrajectoryModel`, `TrajectoryPlayhead`) include **no** JUCE headers — unit-tested by `VectronTests` (Catch2 only).
- **Real-time safety:** no allocations or blocking locks in `processBlock` / voice render.
- **Param IDs:** `snake_case` per PRD §8. Choice-param option order is load-bearing (enums are the contract).

## Design decisions (locked)

1. **Block-rate playhead + per-sample smoothing.** The playhead advances once per render block (PRD: "control-rate is enough; smooth the output"); its `(x, y)` output feeds two `juce::SmoothedValue`s (~20 ms) read per sample. No sub-block machinery.
2. **Point data is state, not params** (PRD §5.2.1). Points live in a `ValueTree` child `"TRAJECTORY"` of `apvts.state` (children `POINT` with `x`, `y`, `timeMs`, `beats`, `tension` properties) and serialize through the existing XML round-trip. Only the macro-controls are APVTS params.
3. **Lock-free-enough handoff:** the processor owns a `TrajectoryModel sharedModel` + `juce::SpinLock` + `std::atomic<int> modelVersion`. Message thread (a `ValueTree::Listener` + `setStateInformation`) rewrites under the lock and bumps the version; the audio thread **try-locks** on version change and copies into its snapshot, keeping the last-good model on failure. Never blocks the audio thread. `replaceState` swaps the APVTS root tree, so the listener re-attaches (and the `"TRAJECTORY"` child is created if absent) in `setStateInformation`.
4. **`TrajectoryModel` is a POD**: `Point { float x, y, timeMs, beats, tension; }`, `Point points[16]`, `int numPoints` (2–16, default 4); trivially copyable, ~336 bytes. `P0.timeMs`/`P0.beats` are ignored (P0 is the note-on start point).
5. **Default point set = the 4 oscillator corners** A(-1,+1) → B(+1,+1) → D(+1,-1) → C(-1,-1), 500 ms / 1 beat per segment, tension 0 — audible out of the box and matches the PRD default of 4 points.
6. **Timing:** segment `i-1→i` lasts `points[i].timeMs / traj_rate` (rate 0.25×–4× is a speed **multiplier**; higher = faster). Synced: `points[i].beats × (60 / bpm) / traj_rate`. This is the locked interpretation of PRD "time values are interpreted as tempo divisions and `traj_rate` becomes a base-division selector": per-point `beats` carries the division count; the continuous rate still scales it. BPM from the host playhead, fallback 120 (Phase 5 precedent). The voice never sees BPM — the processor resolves sync to a per-segment seconds scale.
7. **Modes.** `Off`: blend bypassed — position formula identical to Phase 5 (`base + lfo + mods`), playhead idle. `One-Shot`: P0→Pn once, then holds at Pn. `Loop`: P0 travels forward to `loopStart`, then loops `[loopStart, loopEnd]` indefinitely (independent of note release; the voice dies by amp env as usual). `Loop+Sustain`: loops while the note is held; on release, exits the loop from the current position, travels **forward** to Pn, then holds.
8. **`loopDir`.** `Forward`: on reaching `loopEnd`, wrap to `loopStart` — a position snap; the output smoothing glides it. `Reverse`: on entering the loop at `loopStart`, snap to `loopEnd` and travel backward to `loopStart`, repeating (snap at each wrap, symmetric to Forward; backward segments reuse each segment's own duration). `Ping-Pong`: flips direction at both loop ends, reusing segment durations.
9. **Interpolation.** `Linear`: straight segments, constant speed. `Smooth`: cosine-eased timing per segment **plus** per-point `tension` (-1…+1) bowing the segment as a quadratic Bézier whose control point is offset perpendicular to the chord, scaled by tension (0 = straight, per PRD). Output clamped to [-1, +1]. In `Linear` mode tension is ignored.
10. **Master playhead always runs in Loop semantics.** The processor advances one free-running instrument-level playhead per block that always follows `Loop` behavior over the loop region regardless of `traj_mode` (idle only when mode is Off) — it has no note to release, so Loop is the only meaningful free-run. `Per-Note` + `traj_retrigger` **on** (default): each voice restarts from P0 at note-on. Retrigger **off**: the voice copies the master playhead state at note-on (phase latch) and then follows its own mode semantics from there. `Global`: voices don't advance their own playhead — they use the master's block position (same pattern as Phase 5 global LFOs); One-Shot and Loop+Sustain are therefore Per-Note-trigger behaviors, documented as such.
11. **Unified blend formula** (PRD §5.2), replacing the Phase 5 per-sample position line:
    ```
    depth = (mode == Off) ? 0 : clamp01(traj_depth + dest[DstTrajDepth])
    fx = clamp(baseX·(1−depth) + trajX·depth + lfoX + dest[DstVectorX], −1, +1)
    fy = clamp(baseY·(1−depth) + trajY·depth + lfoY + dest[DstVectorY], −1, +1)
    ```
    `DstTrajDepth` is appended at the end of the `Dest` enum and choice array ("Traj Depth"), keeping existing preset indices valid; full-scale ±1 additive, then clamped 0–1. Depth mod is evaluated per sample like all matrix destinations; `trajX/trajY` come from the block-rate smoothers.
12. **Edge cases clamp, never divide by zero:** `numPoints < 2` → hold at P0; `loopStart/loopEnd` clamped to `[0, numPoints−1]`; `loopStart ≥ loopEnd` → hold at `loopStart` while looping; segment durations clamped to ≥ 1 ms (PRD range 1–10000 ms); point coordinates clamped to ±1 on parse; model shrinking under a live playhead → indices clamped on the next advance.
13. **`traj_recPoints` (int 4–16, default 8) ships dormant** — parsed and stored, used by the recording UX in the GUI phase.

## Architecture

### New files

| File | Purpose | Depends on |
|---|---|---|
| `source/dsp/osc/VectorTrajectory.h` | `vectron::TrajectoryModel` (POD point list) + `vectron::TrajectoryPlayhead` (`{segIndex, segPhase, dir, stage}`, `noteOn`, `release`, `latchFrom`, `advance(model, macros, dtSeconds)` → `(x,y)`). Header-only, JUCE-free. | `<cmath>` |
| `source/params/TrajectoryState.h` | `ValueTree ↔ TrajectoryModel` conversion + `createDefaultTrajectory()` (JUCE-side helper). | JUCE, `VectorTrajectory.h` |
| `tests/test_vector_trajectory.cpp` | Playhead/model unit tests. | Catch2 |

### Modified files

- `source/params/ParameterLayout.cpp` — 11 new params (table below); `DstTrajDepth` choice string; `static_assert` guards updated.
- `source/dsp/mod/ModMatrix.h` — append `DstTrajDepth` to `Dest`.
- `source/dsp/VectronVoice.h/.cpp` — playhead member, two trajectory `SmoothedValue`s, macro params + `const TrajectoryModel*` snapshot pointer + master playhead state in `VectronVoiceParams`, unified blend formula, `startNote`/`stopNote` trigger handling.
- `source/PluginProcessor.h/.cpp` — 11 cached param pointers (null-checked), `ValueTree::Listener` for `"TRAJECTORY"`, shared model + SpinLock + version, audio-side snapshot, master playhead advance, per-block push to voices.
- `tests/CMakeLists.txt` — add the new test file.
- `tests/smoke_main.cpp` — trajectory acceptance checks.

## Parameters (APVTS)

| ID | Type | Range / choices | Default |
|---|---|---|---|
| `traj_mode` | choice | Off, One-Shot, Loop, Loop+Sustain | Off |
| `traj_depth` | float | 0 … 1 | 1.0 |
| `traj_rate` | float | 0.25 … 4 (log skew) | 1.0 |
| `traj_sync` | bool | — | off |
| `traj_loopStart` | int | 0 … 15 | 0 |
| `traj_loopEnd` | int | 0 … 15 | 3 |
| `traj_loopDir` | choice | Forward, Ping-Pong, Reverse | Forward |
| `traj_interp` | choice | Linear, Smooth | Linear |
| `traj_trigger` | choice | Per-Note, Global | Per-Note |
| `traj_retrigger` | bool | — | on |
| `traj_recPoints` | int | 4 … 16 | 8 |

## Testing

**Catch2 (`test_vector_trajectory.cpp`, JUCE-free):** segment timing math (ms and beats·bpm paths, rate scaling); one-shot travel and hold at Pn; forward-loop wrap to `loopStart`; reverse loop; ping-pong direction flips reusing segment durations; loop+sustain release path to Pn (including release while traveling backward); linear vs smooth timing; tension 0 ≡ straight, tension ≠ 0 bows off-chord; retrigger latch copies master state; all edge cases in decision 12.

**Smoke (`VectronSmoke`):** (a) `traj_mode` Off vs Loop renders audibly differ over time; (b) One-Shot: the late window of the render matches a static render placed at Pn's position; (c) a matrix routing into `Traj Depth` changes the render. Existing 68 tests stay green.

## Out of scope (deferred)

- XY-pad trajectory editing, record-arm capture, resampling to `traj_recPoints` (GUI phase, §9).
- Trajectory-rate matrix destination (not in PRD).
- Unison (Phase 7), master FX (Phase 8).
