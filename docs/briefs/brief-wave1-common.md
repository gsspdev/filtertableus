# Wave 1 common rules — FilterTableUS (read before your agent brief)

You are one of six agents building FilterTableUS in parallel in the SAME working tree: `/Users/music/Developer/filtertableus`. The Phase 0 scaffold is complete: build system, frozen interface headers, compiling stubs, and a plugin that already passes pluginval/auval as a passthrough.

## Required reading, in order
1. The plan: `/Users/music/.claude/plans/make-a-plan-to-crispy-hopcroft.md` (sections cited in your brief).
2. `docs/INTERFACES.md` and `docs/ARCHITECTURE.md` in the repo.
3. **The actual frozen headers** in `core/include/ftc/` and `include/ftus/` — these are AUTHORITATIVE over any signature sketch in the plan or briefs. Code against what is really there.
4. Your agent brief.

## Hard rules
- **File ownership is absolute.** Create/modify/delete files ONLY under the paths your brief lists as owned. Never touch `core/include/ftc/`, `include/ftus/`, `source/plugin/`, other agents' directories, or shared CMake files. If a frozen interface is wrong/incomplete, DO NOT change it — implement the best you can against it and report the problem prominently in your final report (the integration agent fixes seams).
- Other agents are editing their own directories concurrently. If you see unrelated files appear or change, ignore them. Never run repo-wide formatters, `git add`, `git commit`, `git checkout`, or anything that touches state outside your owned paths.
- **Use your own build directory** exactly as named in your brief (`build/<agent>`), never a shared one. CMake source lists for `core/` and test dirs use GLOB with CONFIGURE_DEPENDS — adding a file under your owned paths is picked up on reconfigure; you should not need to edit any CMakeLists outside your owned dirs (per-directory CMakeLists you own are listed in your brief).
- Long builds can exceed the 10-minute foreground command cap — run them in the background and wait, or build specific targets.
- Real-time-safety rules for anything on the audio path: no allocation, locks, logging, or exceptions after `prepare()`; all buffers sized worst-case in `prepare()`.
- Iterate until YOUR acceptance criteria pass (build green + your tests green). Do not stop at "mostly works".

## Final report (your last message; the orchestrator reads only this)
1. What you built (short). 2. Exact test/build commands and their results (pass counts). 3. Deviations from plan/brief and why. 4. Interface problems you hit (frozen-header gaps, stub limitations) — be specific. 5. Anything the integration agent must do to wire your work fully.
