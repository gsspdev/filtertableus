# Agent D — Modulation engine (core) + State/Presets

Read `/Users/music/.claude/jobs/39205b91/tmp/brief-wave1-common.md` first. Plan sections: §2 "Modulators", §3 "State & presets".

## Owns (exclusive)
- `core/src/mod*.{h,cpp}` (real `ftc::ModulationEngine`; replace/delete the Phase 0 `mod_stub.cpp`)
- `core/tests/test_mod*.cpp`
- `source/state/**` (including its CMakeLists; replace the Phase 0 `StateManagerStub.cpp`)
- `resources/presets/factory/**` (the .ftpreset XMLs; if the `ftus_presets` binary-data file list lives in frozen CMake, note it for integration instead of editing)
- `tests/unit/state/**`
- Build dirs: `build/d-core` (core-only, for mod work) and `build/d` (full, for state work)

## Part 1 — `ftc::ModulationEngine` (per plan §2 "Modulators"; frozen header `Modulation.h`)
- Two LFOs: shapes sine/triangle/sawUp/sawDown/square/S&H (bipolar −1..+1). Free-rate Hz, or tempo-synced using the 16 `SyncDivision`s (8/1…1/32 with dotted/triplet; compute beats-per-cycle for each). **Timeline-locked when playing**: phase = frac(ppqPosition / beatsPerCycle + 0) so loops/jumps stay coherent; free-run from current BPM when stopped; `retrigger` → reset phase on any note-on in the block. S&H: new random value each cycle wrap, seeded deterministically, reseed on transport start.
- Envelope follower: per-sample rectified mono input, one-pole attack/release (a = 1 − exp(−1/(τ·fs))), sensitivity dB applied as input gain, output clamped 0..1; `beginBlock` consumes the block's mono input; `evaluate(offset)` samples the follower state at that offset (advance internally at control-tick granularity — per-sample accuracy within the block is not required as long as tests pass tolerances).
- Note tracker: last-note priority from `NoteEvent`s, hold last value on all-notes-off.
- `evaluate(subBlockOffset)` returns summed `ModValues`: scanOffset = lfo1·toScan + lfo2·toScan + env·envToScan (each toScan ±1 = full range); cutoffSemis = (lfo1·lfo1ToCutoff + lfo2·lfo2ToCutoff + env·envToCutoff)·48 + keytrack·(lastNote − 69). The ENGINE clamps final values — you just sum.
- RT-safe after prepare; deterministic given the same inputs.

## Part 2 — `StateManagerImpl` (frozen header `include/ftus/StateManager.h`; plan §3)
- Full session state XML: `<FilterTableUS stateVersion="1" pluginVersion="...">` wrapping the APVTS state, a `<WAVETABLE>` node via the Phase 0 `WavetableCodec` (type user/converted → embedded gzip+base64 payload; type factory → factoryId only, regenerate on load via `generateFactoryTable` + `analyze` — if agent B's generators aren't merged yet, code against the frozen `FactoryTables.h` declaration; it links because B is building it in parallel; if it doesn't link in your build, stub the call behind the interface and note it), `<GUI scale>`, `<PRESET name dirty>`.
- `setState`: parse → `apvts.replaceState` → decode wavetable → hand to the processor's adoption path (see how `source/plugin/` exposes it — there is a hook for the StateManager/loader results; if the seam is missing, implement decode + document exactly what integration must wire). Must be safe when called off the message thread (no GUI calls without `MessageManager` guard).
- Presets: `.ftpreset` files (same XML schema, root `FTUSPreset`) in `~/Library/Application Support/FilterTableUS/Presets/`; factory presets from the `ftus_presets` binary data listed first, read-only; flat list, `next/prev` wrap, `saveUserPreset` writes (create dirs; overwrite allowed), `isDirty` = any param/table change since load (APVTS listener + table-change flag).
- Author **8–10 factory presets** (XML, referencing factory table IDs + musical param settings across the four phase modes and both mod sources — e.g. a synced-LFO scan sweep bass, an env-follower auto-wah-like patch, a Linear-mode parallel EQ-ish patch, a Raw-mode mangler).

## Acceptance (Catch2; mod tests JUCE-free in core, state tests in tests/unit/state)
1. LFO: each shape hits expected extrema; measured period at 48 kHz within 0.5% for 1 Hz free; synced 1/4 at 120 BPM = 0.5 s period; phase continuity across evaluate calls; timeline lock: evaluate at ppq 3.0 equals evaluate at ppq 7.0 for a 1-bar cycle (4/4).
2. Env: step input reaches 1−1/e of final at attackMs ± 10%; release symmetric; sensitivity ±dB scales.
3. Keytrack: note 81 (A5) with keytrack 1 → +12 semis; keytrack −0.5 → −6.
4. State: full round-trip (params + embedded user table) → APVTS values equal + decoded table sample-identical; factory-type state regenerates the identical table (if B's generators link) or the documented fallback; a saved v1 golden blob checked into tests keeps loading.
5. Presets: save → appears in list → load → params match; next/prev wrap; dirty flag set on param change, cleared on load/save.
6. All existing suites remain green in your build dirs.
