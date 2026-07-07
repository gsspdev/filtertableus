# Wave 3 — Integration, polish & release brief

All Wave-1/2 workstreams are landed and green (engine full-build: 135/135 incl. the harness's
real-engine tier-B assertions). `source/plugin/`, `include/ftus/`, `core/include/ftc/` are now
UNFROZEN for you (and only you). Read first: docs/briefs/brief-wave1-common.md (rules — git is
still orchestrator-only), docs/INTERFACES.md, docs/PLAN.md §7–§9, and the workstream reports in
`~/.claude/jobs/39205b91/tmp/wave1-reports/` (A1 afb558ac, A2 a87b61b2, E ae56f638, D a1adfc15).

## Integration checklist (collected from agent reports — verify each, fix, re-run suites)

1. **Shell `prepareToPlay` ordering** (engine agent): fill an `ftc::Parameters` from the cached
   atomics and call `engine.setParameters(...)` BEFORE `engine.prepare(...)` in
   `FtusAudioProcessor::prepareToPlay`, so a session restored to Linear/Original reports its L/2
   latency immediately instead of after the first block.
2. **Bypass through the dry delay** (plan §2; engine agent): the shell's parameter-bypass early
   return skips latency compensation. Route bypassed audio through the engine's latency-matched
   dry path (add a small engine call or process-with-mix-0 equivalent) so bypass stays
   time-aligned in Linear/Original modes. Keep pluginval green.
3. **CLAP parameter text round-trip** (agent E): clap-validator's `param-conversions` test fails
   because the layout in `source/plugin/Parameters.cpp` lacks explicit
   `stringFromValue`/`valueFromString` attributes. Add precise conversions (Hz/dB/ms/% with
   sensible decimals, exact round-trip) to all float params, then flip the clap-validator step
   from advisory to required in `scripts/run_all_checks.sh` and confirm it passes.
4. **LINEAR half-sample center decision** (A2/engine agents): `FTUS_LINEAR_HALF_SAMPLE_CENTER`
   defaults to 1 (kernel symmetric about (L−1)/2 → broadband 50%-mix null is a half-sample comb).
   DECIDE: flip to 0 (exact integer-sample center; engine tests auto-tighten to the strict
   −60 dBFS null) unless listening reveals a downside. Document the choice in EngineConfig or
   the kernel generator header.
5. **GUI scale persistence** (GUI agent): migrate the root-property `"guiScale"` into the
   `<GUI scale=...>` node of StateManagerImpl's schema (or bless the current property and update
   docs/PLAN.md §3) — one consistent home, session-safe.
6. **Latency renotify sweep**: with 1–4 in place, run pluginval strictness 8 (10 as stretch),
   auval, and the full ctest matrix again; also `build/core` (FTUS_BUILD_PLUGIN=OFF) and a
   **universal build** (`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, e.g. `build/universal`) —
   both slices must build; run `arch -x86_64 auval -v aumf Ftbl FtUs` if Rosetta is available,
   else note it.
7. **Factory preset audition pass**: load each of D's presets in the standalone, confirm every
   one produces audible, musical filtering (real engine now!) and the preset bar's dirty flag +
   prev/next behave. Fix preset parameter values that don't survive contact with the real DSP
   (they were authored against the stub).
8. **End-to-end DAW-less sanity**: standalone with live audio-file playback or the synthesized
   render harness — sweep Scan on a saw through each phase mode; verify no zipper audibly and
   the LINEAR mode nulls against dry at mix 50% (post-decision #4); screenshot the final GUI
   with a loaded table + response curve visible (the spectrum should now show real curves, not
   the flat stub line) into build/artifacts/final-*.png.
9. **Docs truth pass**: update docs/ARCHITECTURE.md + docs/INTERFACES.md "warts" sections and
   docs/EXECUTION.md checkboxes to the as-built state; note the mode-switch transition is
   10 ms + new-mode latency; ResponseCurve = FFT of the built kernel.
10. **Calibration TODO list** (do NOT attempt by-ear matching yourself): write
    docs/CALIBRATION.md listing the plan §9 risk-register knobs with their code locations
    (resonance k_r curve/reference, scan-lerp domain switch, LowEndPolicy, RAW normalization,
    LINEAR window α, update cadence) so a human with the original plugin can A/B them.

## Acceptance
`scripts/run_all_checks.sh build/full` → ALL CHECKS PASSED with clap-validator REQUIRED and
green; universal build green; 100% ctest everywhere; final screenshots present; docs updated.
Report: per-item disposition (fixed/decided/deferred+why), commands+results, anything left for
the human (calibration list, DAW-in-anger testing).
