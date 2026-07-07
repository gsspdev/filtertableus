# Agent C — GUI (editor, waterfall, spectrum, panels, look-and-feel)

Read `/Users/music/.claude/jobs/39205b91/tmp/brief-wave1-common.md` first. Plan section: §5 (authoritative layout/behavior spec) + §3 for parameter IDs.

## Owns (exclusive)
- `source/gui/**` (including its `CMakeLists.txt`; replace the Phase 0 stub `PluginEditor.cpp`)
- `resources/fonts/**` (download Inter — SIL OFL licensed — regular + medium TTFs from https://github.com/rsms/inter/releases, keep its LICENSE file; wire into the `ftus_assets` binary-data target if that target's file list lives in a CMakeLists you own — if it lives in frozen CMake, note it for integration instead of editing)
- Build dir: `build/c` (full plugin build)

## What to build (per plan §5 — dimensions and component tree are specified there)
- `PluginEditor` 920×620 design space, resizable 0.75–1.75× fixed aspect via constrainer, scale persisted (GUI node in state — if the StateManager seam isn't ready, keep scale in a member + TODO note). All layout computed from the design space in `resized()`.
- `Theme.h` + `FtusLookAndFeel` (LookAndFeel_V4): bg `#141619`, panels `#1C1F26`, strokes `#2A2E37`, text `#C9CED6`, accent `#FF8A3D`, warning `#E5484D`; 270° arc rotaries with value arc + pointer; center-notched bipolar variant (fill from 12 o'clock) for keytrack/depth knobs; Inter font embedded via binary data (fall back to system font if the asset target can't be modified — note it).
- `PresetBar`: prev/next arrows, preset name button (popup menu), Save, dirty `*`, "FilterTableUS" logo text. Drive via the `StateManager` interface only (Phase 0 stub returns empty lists — that's fine, the UI must handle empty gracefully).
- `WavetableRow`: table name + frame count, Load… (async file chooser → `LoaderService::requestLoadFile`), factory-table popup listing `FactoryTables.h` display names (→ `requestFactoryTable`), thin progress bar (poll `progress()`), 4 s error toast on failed `LoadResult`.
- `WaterfallView`: pseudo-3D stacked polylines — ≤48 evenly-strided frames back-to-front, each decimated to 128 points, per-depth offset (+2.4, −4.6 px scaled), alpha 1.0→0.18 toward the back; active frame (engine `currentScan()` atomic polled on a timer) drawn last in accent with soft under-fill. Click/vertical-drag maps to the `scan` parameter via `juce::ParameterAttachment` with proper begin/end gestures; shift = fine; double-click = reset. Frame polylines cached as `juce::Path` per table+size (rebuild only on table change/resize); repaint ~30 fps gated by a dirty flag (scan moved > 1/512 or table changed). Get the wavetable frame data for drawing from the processor's currently-published table (there is an accessor on the processor or via the engine — check `source/plugin/` headers; if no clean accessor exists, note it for integration and draw from the last `LoadResult` you observed).
- `SpectrumView`: log-f 20 Hz–20 kHz, −30…+30 dB grid with decade/octave gridlines; 30 Hz timer calls the engine's `readResponseCurve` (via a processor accessor); rebuild path on new data; filled translucent accent + 1.5 px stroke. Frequency mapping MUST use `ftc::ResponseCurve::frequencyForPoint`.
- `KnobPanel` (Scan/Cutoff/Resonance/Mix, 88 px), small-knob row (Keytrack, Output), `PhaseModeSelector` (4 flat segments, accent underline, tooltips noting latency for LIN/ORIG), `ModPanel` (tabs LFO1|LFO2|ENV; LFO tab: Rate knob ⇄ Div combo swapped by Sync toggle, Shape combo, Retrig toggle, 2 bipolar depth knobs; ENV tab: Sens/Attack/Release + 2 depth knobs + live env meter from `envValue()`), shared `ValueReadout` strip (hover/drag shows param name + formatted value).
- Whole editor is a `FileDragAndDropTarget` for .wav (highlight overlay while hovering → `requestLoadFile`).
- Editor↔processor rules (docs/INTERFACES.md): attachments only for control; polling only for visuals; never call the engine directly from paint; no locks.

## Acceptance
1. `build/c` full build green; plugin opens (Standalone) at 920×620 and resizes cleanly (run `scripts/smoke_standalone.sh build/c` or launch Standalone in background + `screencapture -x` a PNG into `build/artifacts/gui-c.png`; view the screenshot yourself and iterate until the layout actually matches the plan — no overlaps, readable text).
2. Every one of the 27 parameters is bound to a control via an attachment (write a small unit or runtime assertion walking the APVTS and your attachment registry).
3. Waterfall drag changes `scan` with begin/end gestures (verify via APVTS listener in a test or manual standalone check).
4. Spectrum renders the stub engine's flat curve; no repaint storms (paint gated as specified).
5. Drop / Load / factory-menu paths call the LoaderService (stub will error — the toast must render).
6. All Catch2/ctest suites still green.
