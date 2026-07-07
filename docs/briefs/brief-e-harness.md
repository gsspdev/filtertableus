# Agent E — Integration test harness, validation scripts, CI

Read `/Users/music/.claude/jobs/39205b91/tmp/brief-wave1-common.md` first. Plan sections: §7 (workstream E row), §8 verification.

## Owns (exclusive)
- `tests/integration/**` (including its CMakeLists)
- `scripts/**` (hardening the Phase 0 validate/smoke scripts is yours)
- `.github/**`
- Build dir: `build/e` (full plugin build, headless test runs)

## What to build
1. **Golden parameter-list test**: instantiate the processor headless (`juce::ScopedJuceInitialiser_GUI`), walk the APVTS, assert the EXACT set of 27 parameter IDs with their ranges/defaults/choice counts per plan §3 (read `include/ftus/PluginIDs.h` as the source of truth; hard-fail on any drift, extra, or missing param). Also assert `getBypassParameter()` is wired and latency is reported ≥ 0.
2. **Offline render suite** through the real processor (no host): prepare(48k, 512, stereo) → load a factory table if the loader links (else synthesize a `WavetableData` directly via `analyze`) → render 2 s of a 100 Hz→10 kHz sine sweep and 2 s of white noise per phase mode, with a mid-render `scan` automation ramp. Assertions **parameterized to tighten automatically**: (a) always: output finite, no NaN/inf, RMS within [−80, +6] dBFS bounds, reported latency constant during render, state save/load mid-render doesn't corrupt output; (b) when the engine is no longer the passthrough stub — detect BEHAVIORALLY (load a strongly-filtering table, process noise, check wet != dry); do NOT use the static `latencySamplesFor` for stub detection: it already returns the real design values (L/2 for Linear) even under the stub, whose *instance* `latencySamples()` stays 0: wet differs from dry (filter is doing something), mix=0 output equals the input delayed by the reported latency (null < −60 dBFS), per-mode instance latency matches `latencySamplesFor`. Write the suite so (b) is skipped-with-notice while the stub is in place and runs automatically once the real engine lands.
3. **setWavetable stress test**: hammer table swaps at ~50 Hz from a second thread (via the processor's adoption path) during a render; assert no crash, output finite (this validates the ObjectHandoff/graveyard design).
4. **State round-trip integration test**: full processor state (params + table) through get/setStateInformation, bit-exact params after reload.
5. **Scripts hardening**: `validate_vst3.sh`, `validate_au.sh`, `validate_clap.sh`, `smoke_standalone.sh`, and `run_all_checks.sh` ALREADY EXIST (Phase 0, default build dir `build/full`); pluginval L8 and auval already pass on the stub plugin. Your jobs: (a) fix `validate_clap.sh` — its clap-validator download URL 404s; find the correct GitHub release asset name for macOS (inspect https://github.com/free-audio/clap-validator/releases) and keep the clean SKIP fallback; (b) keep exit codes and PASS/FAIL summaries reliable as you extend the scripts.
6. **CI draft** `.github/workflows/ci.yml`: macos-14, cache `_deps`, configure Ninja Release (native arch for speed), build, ctest, pluginval level 8, auval, upload artifacts + screenshot. It won't run here — keep it plausible and documented.

## Acceptance
- `build/e` green; the golden test, render suite (stub tier), stress test, and state test all pass via ctest.
- `scripts/run_all_checks.sh build/e` completes with PASS lines for build/ctest/pluginval/auval (clap may SKIP if no validator; smoke produces a screenshot).
- Print in your report which assertions are in the "tighten when real engine lands" tier so the Wave-3 integrator knows what flips on.
