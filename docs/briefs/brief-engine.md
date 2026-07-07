# Wave 2 agent — Real `FilterTableEngine` assembly

Read `/Users/music/.claude/jobs/39205b91/tmp/brief-wave1-common.md` first. Plan section: §2 end-to-end (this is the integration of A1's convolver + A2's kernel generator + D's modulation into the facade). A1 and A2 have LANDED in the tree (their tests pass); agent D's modulation may still be in flight — if `ftc::ModulationEngine` is still the zero-stub, integrate against the interface anyway (your mod-related tests then assert the zero-mod behavior and note it; the interface is frozen so no rework is needed when D lands).

## Owns (exclusive)
- `core/src/engine*.{h,cpp}` — implement the real `ftc::FilterTableEngine` (frozen header `FilterTableEngine.h`); DELETE `core/src/engine_stub.cpp` in the same change so exactly one definition exists.
- `core/tests/test_engine*.cpp`
- Build dir: `build/engine` (core-only `-DFTUS_BUILD_PLUGIN=OFF` for iteration; run one full-plugin build `build/engine-full` at the end to prove the shell links).

## What to build (`process()` per block)
1. Guard: not prepared → output silence. `ScopedNoDenormals`. Adopt any pending wavetable via ObjectHandoff at block start (table change → treat like a kernel change: request rebuild; if latency-neutral, normal crossfade).
2. `ModulationEngine.beginBlock(transport, notes, monoIn, n)` (mono tap = 0.5·(L+R) into a preallocated scratch).
3. Chop the block at **control ticks (64)**: per tick — `mod.evaluate(offset)` → effective targets: scanT = clamp01(params.scan + mv.scanOffset); cutoffT = clamp(params.cutoffHz · 2^(mv.cutoffSemis/12), 20, min(22000, 0.45·fs)); resT = clamp(params.resonance, −1, 1) → one-pole smoothers (15 ms; cutoff smoothed in log2 domain).
4. At **kernel ticks** (`EngineConfig::kernelUpdateInterval(fs)`): change detector (|Δscan| > 2⁻¹², |Δlog2 fc| > 1/1200, |Δres| > 2⁻¹⁰, or mode/table changed) → if changed and `!section.isFading()`: `generator.generate(request, kernel)` INLINE (audio thread; it is RT-safe by A2's contract) → `section.pushKernel(kernel)`. Publish a fresh `ResponseCurve` (sample the generator's target magnitude response at the 256 log-spaced points — add a small helper INSIDE your engine implementation files that reuses A2's response construction; if A2 exposed no hook, compute from `measure`-style probing of the kernel spectrum, or better: call generate and FFT the kernel — pick the cheapest correct option and document it) into the TripleBuffer. Update `currentScan`/`envValue` atomics.
5. **Mode switch** (params.mode != active mode): 5 ms wet fade-out → `section.setKernelImmediate(newModeKernel)` + convolver reset + dry-delay length switch to `latencyForMode(newMode)` → 5 ms fade-in. `latencySamples()` reflects the new mode immediately (the shell's AsyncUpdater notifies the host). No zipper/clicks from the fade itself.
6. Wet path: `section.process(channels, n)` in a scratch copy; dry path: original input through a `DelayLine` of exactly the current reported latency; output = per-sample-ramped mix (10 ms) of dry/wet + per-sample-ramped output gain (dB→linear).
7. Robustness: per-block finiteness scan of wet; on NaN/inf → reset section, silence the block, resume. Silence-idle: after > L + one tick of input below −180 dBFS, clear state once and output zeros cheaply until signal returns (still honor param/mode changes so resume is fresh). `prepare` allocates EVERYTHING worst-case (max block, kernel L for fs, both convolver instances, delay line L/2, scratches) and builds the initial kernel synchronously so the first block is valid. `reset()` clears state, keeps kernels.
8. `latencySamplesFor(mode, fs)` = `KernelGenerator::latencyForMode(mode, EngineConfig::kernelLength(fs))`.

## Acceptance (Catch2, core-only, deterministic)
1. Per-mode latency: impulse through each mode (flat-ish table) → onset/peak exactly at `latencySamplesFor(mode, 48000)`; reported value matches.
2. LINEAR null: flat table, mix 0.5 → output vs input-delayed-L/2 residual < −60 dBFS. MINIMUM + flat table → null vs undelayed input < −60 dBFS.
3. Chunking invariance: identical output (bit-exact) for block-size sequences {512}×N vs {1,17,64,441,4096,…} covering the same samples, all modes, with scan automation running.
4. Zipper: 2 s noise, scan 0→1 and cutoff 100→10k sweeps (separately + together, all 4 modes) → click detector (helpers) max normalized second-difference ≤ 4× the static-parameter baseline.
5. Mode switch mid-render: no discontinuity beyond the fade envelope's own shape; no NaN; latency value switches immediately.
6. NaN injection: one NaN input sample → output finite again within one block, stays finite.
7. Denormal: impulse → 2 s silence → engine state decays to exact zeros; no subnormals in output.
8. Determinism: two identical renders bit-identical.
9. Full-plugin build `build/engine-full` links and `ctest` there is green (integration seed test now exercises the real engine — its latency-0 assertion may need updating ONLY if that test lives in a file you own; if it's E's file, report the needed change instead of editing).
