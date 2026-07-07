# FilterTableUS — Execution state & resume guide

> Written by the orchestrating session on **2026-07-07 ~01:50 PDT** so a fresh session (or human) can resume
> without the original context. Spec: `docs/PLAN.md`. Agent briefs: `docs/briefs/` (canonical copies also at
> `~/.claude/jobs/39205b91/tmp/` — the running workflow references the tmp paths; the repo copies are identical).

## Where things stand (update this file as milestones land)

- [x] Plan researched, designed, user-approved (see docs/PLAN.md incl. amendments)
- [x] Dependencies pre-cloned + pinned in `external/` (JUCE 8.0.14, clap-juce-extensions 16e9d4c, Catch2 v3.7.1, pffft a4b0359 — see external/PINS.txt)
- [x] All agent briefs written (docs/briefs/)
- [x] **Phase 0 scaffold — DONE 2026-07-07 ~02:25** (built INLINE by the orchestrator after two
      delegated scaffold agents stalled on hung early tool calls; workflow runs killed).
      Evidence: native arm64 Release builds VST3+AU+CLAP+Standalone; ctest 22/22 (15 ftcore +
      7 shell/integration); pluginval strictness 8 SUCCESS; auval (aumf Ftbl FtUs) PASS;
      standalone screenshot in build/artifacts/. clap-validator SKIPped (download URL 404 —
      assigned to Wave-1 agent E). JUCE 8.0.14 note: splash flag removed upstream (harmless
      warning). Checkpoint items were built-in: editor/loader/state factory seams, GLOB source
      lists, ftus_shared INTERFACE (pamplejuce) test-linking pattern, core-only no-JUCE config.
- [x] Orchestrator checkpoint + **first git commit**
- [x] Wave 1 (A1/A2/B/C/D/E parallel) + Wave 2 (engine, chained on A1+A2) — **DONE 2026-07-07 ~03:55**
      (all six workstreams + real engine landed; full ctest green; pluginval 8 + auval PASS;
      clap-validator advisory-WARN pending Wave 3)
- [x] Orchestrator verification + commit (`Wave 1+2: real DSP engine, wavetable I/O, GUI,
      modulation, state, harness`)
- [x] Wave 3 integration — **DONE 2026-07-07** (integration agent, build dirs `build/w3*`):
      shell prepare-order + latency-aligned bypass; explicit parameter text conversions;
      clap-validator promoted to REQUIRED and fully passing (18/18 run, param-conversions +
      state-reproducibility incl.); `FTUS_LINEAR_HALF_SAMPLE_CENTER=0` decided (strict 50 %-mix
      LINEAR null, −155 dBFS measured); GUI scale single serialized home (`<GUI scale>`);
      universal arm64+x86_64 build green with 137/137 ctest, x86_64 auval via Rosetta PASS and
      the x86_64 core suite green under Rosetta; pluginval strictness 8 AND 10 SUCCESS; factory
      presets auditioned via offline renders (all 10 filter audibly + mutually distinct, no
      value fixes needed); final screenshots `build/artifacts/final-*.png`; docs trued up +
      `docs/CALIBRATION.md` written. Frozen-surface changes logged in docs/INTERFACES.md.
- [ ] Human calibration pass (by ear vs. the original; see docs/CALIBRATION.md) + in-DAW
      manual testing (Live/Logic/Reaper: latency compensation, automation, session reload)
- [ ] Tag v0.1.0 (orchestrator)

## Orchestrator checkpoint after Phase 0 (do these before launching Wave 1)

1. Read the workflow's returned scaffold report + verifier verdict (must be pass=true).
2. Inspect seams the parallel agents depend on (fix directly if missing — Phase 0 isn't frozen until this checkpoint ends):
   - `source/plugin/PluginProcessor.cpp::createEditor()` must reach the GUI via a factory/function implemented in `source/gui/` (agent C replaces the stub file without touching frozen `source/plugin/`).
   - LoaderService / StateManager instances must be constructed via factory functions implemented in `source/wavetable/` / `source/state/` (stub .cpps there now) — NOT `new`-ed concretely inside frozen `source/plugin/`.
   - `core/CMakeLists.txt` and test CMakeLists must pick up new sources without edits (GLOB CONFIGURE_DEPENDS or equivalent) — four different agents add files under `core/src/` and `core/tests/`.
   - Confirm `-DFTUS_BUILD_PLUGIN=OFF` configure touches no JUCE.
3. `git add -A && git commit` (message: "Phase 0: scaffold — build system, frozen interfaces, stubs; pluginval+auval green"). Commits end with:
   `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
4. Append any scaffold-report warts to the relevant `docs/briefs/*.md`.
5. Launch the Wave 1+2 workflow (template below).

## Wave 1+2 workflow script (launch via the Workflow tool; adjust paths only if briefs moved)

```js
export const meta = {
  name: 'ftus-wave12-parallel-build',
  description: 'Six parallel workstreams; engine assembly chained after A1+A2 land',
  phases: [
    { title: 'DSP', detail: 'A1 convolution + A2 kernels, then Wave-2 engine assembly' },
    { title: 'Subsystems', detail: 'B wavetable I/O, C GUI, D modulation+state, E harness' },
  ],
}
const REPO = '/Users/music/Developer/filtertableus'
const B = REPO + '/docs/briefs'
const mk = (name, brief, extra = '') =>
  `You are agent ${name} building part of the FilterTableUS plugin in ${REPO}. Read ${B}/brief-wave1-common.md (rules), then your brief ${B}/${brief}, then the plan sections it cites (${REPO}/docs/PLAN.md) and the actual frozen headers. Execute fully; iterate until your acceptance criteria pass. ${extra}Final message = the report format in the common brief.`
phase('DSP')
const [dsp, b, c, d, e] = await parallel([
  async () => {
    const [a1, a2] = await parallel([
      () => agent(mk('A1 (convolution)', 'brief-a1-convolution.md'), { label: 'A1:convolution', phase: 'DSP' }),
      () => agent(mk('A2 (kernel generation)', 'brief-a2-kernelgen.md'), { label: 'A2:kernelgen', phase: 'DSP' }),
    ])
    if (!a1 || !a2) { log('A1/A2 failed - skipping engine'); return { a1, a2, engine: null } }
    log('A1+A2 landed - starting Wave-2 engine assembly')
    const engine = await agent(mk('Wave-2 (engine assembly)', 'brief-engine.md',
      `A1 report: ${JSON.stringify(String(a1).slice(0, 2500))}. A2 report: ${JSON.stringify(String(a2).slice(0, 2500))}. `),
      { label: 'W2:engine', phase: 'DSP' })
    return { a1, a2, engine }
  },
  () => agent(mk('B (wavetable I/O)', 'brief-b-wavetable.md'), { label: 'B:wavetable', phase: 'Subsystems' }),
  () => agent(mk('C (GUI)', 'brief-c-gui.md'), { label: 'C:gui', phase: 'Subsystems' }),
  () => agent(mk('D (modulation + state)', 'brief-d-modstate.md'), { label: 'D:modstate', phase: 'Subsystems' }),
  () => agent(mk('E (harness + validation)', 'brief-e-harness.md'), { label: 'E:harness', phase: 'Subsystems' }),
])
return { a1: dsp?.a1 ?? null, a2: dsp?.a2 ?? null, engine: dsp?.engine ?? null, wavetable: b, gui: c, modState: d, harness: e }
```

After it returns: full build + `ctest` + `scripts/validate_vst3.sh` + `scripts/validate_au.sh` yourself; fix small seams or spawn a fix agent; commit ("Wave 1+2: DSP engine, wavetable I/O, GUI, modulation, state, harness").

## Wave 3 (after Wave 1+2 verified)

One workflow: (1) integration agent — unfreeze `source/plugin`, wire remaining seams from agent reports, full validation suite, standalone screenshot; (2) adversarial review fan-out — RT-safety lens (allocations/locks on audio thread), correctness lens (plan §2 conformance), UX lens (screenshot vs plan §5); (3) fix agent for confirmed findings; (4) rerun `scripts/run_all_checks.sh`. Then commit + `git tag v0.1.0`. Calibration-by-ear items (plan §9) become a documented TODO list — they need human listening vs. the original plugin.

## Key facts a resuming session must know

- Session task board: 9 tasks (Phase 0 … Wave 3) exist in the task tools; keep statuses current.
- Workflow scripts persist under `~/.claude/projects/-Users-music-Developer-filtertableus/*/workflows/scripts/`; the Phase 0 script is `ftus-phase0-scaffold-*.js` (scaffold+verify+fix loop) — relaunch with `{scriptPath}` if it ever needs a rerun (nothing cached is worth resuming if the scaffold agent didn't complete).
- **Do not** let any subagent `git commit`; the orchestrator commits at wave boundaries only.
- Frozen-after-Phase-0 paths: `core/include/ftc/`, `include/ftus/`, `source/plugin/` (unfrozen only for the Wave-3 integrator).
- Per-agent build dirs (`build/a1`, `build/b`, …) — never share; core agents configure with `-DFTUS_BUILD_PLUGIN=OFF`.
- Known environment quirks: foreground shell commands cap at 10 min (run long builds in background); auval needs the component in `~/Library/Audio/Plug-Ins/Components` (COPY_PLUGIN_AFTER_BUILD handles it; stale cache → `killall -9 AudioComponentRegistrar`); pluginval binary cached in `scripts/.cache/`.
