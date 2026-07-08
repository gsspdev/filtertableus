# FilterTableUS

A from-scratch, original-code clone of the Kilohearts "Filter Table" concept:
wavetable frames are interpreted as filter frequency responses, and scanning
through the table smoothly morphs the filter. Scan / Cutoff (harmonic-24
mapping, keytrackable) / Resonance / Mix, four phase modes (Minimum, Linear,
Original, Raw), internal modulation (2 LFOs, envelope follower, MIDI
keytracking), wavetable import and sample-to-wavetable conversion.
JUCE 8 / C++20. Formats: VST3, AU (aumf), CLAP, Standalone (macOS first).

## Build

Dependencies are used from local pinned checkouts in `external/`
(JUCE 8.0.14, clap-juce-extensions, Catch2 v3.7.1 — see `external/PINS.txt`);
if `external/` is absent, CMake falls back to `FetchContent` with the same
pinned commits. pffft is vendored under `core/third_party/pffft/`.

```bash
# Full plugin build (native arch; add -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for universal)
cmake -B build/scaffold -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/scaffold
ctest --test-dir build/scaffold --output-on-failure

# JUCE-free core-only loop (DSP agents iterate here; no network, no JUCE)
cmake -B build/core -G Ninja -DFTUS_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core
ctest --test-dir build/core --output-on-failure

# Validation
scripts/validate_vst3.sh    # pluginval, strictness level 8
scripts/validate_au.sh      # auval -strict -v aumf Ftbl FtUs
scripts/validate_clap.sh    # clap-validator if available, else skips
scripts/smoke_standalone.sh # launches the standalone app, screenshots it
```

Builds default to the native architecture for fast iteration; release/validation
builds pass `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. Each parallel agent must use its own build directory
(`build/<agent>`) — concurrent builds in a shared directory race.

Repo layout, interface contracts and threading rules: `docs/ARCHITECTURE.md`
and `docs/INTERFACES.md`. Headers under `core/include/ftc/` and
`include/ftus/` are frozen after Phase 0.

## License

AGPL-3.0-or-later (see `LICENSE`). JUCE is used under its AGPLv3 option with
the splash screen disabled. pffft (BSD-like) and Catch2 (BSL-1.0) keep their
own licenses. The bundled Inter font is used under the SIL Open Font License
1.1 (see `resources/fonts/LICENSE.txt`). Not affiliated with Kilohearts; no
Kilohearts code or assets.
