// FilterTableUS core — the REAL ftc::FilterTableEngine (Wave-2 engine assembly).
// Replaces core/src/engine_stub.cpp behind the frozen header ftc/FilterTableEngine.h.
//
// Per-block pipeline (docs/PLAN.md §2, docs/briefs/brief-engine.md):
//   1. Guard (not prepared -> silence), ScopedNoDenormals, adopt any pending wavetable via
//      ObjectHandoff at block start (a table change requests a kernel rebuild through the
//      normal crossfade path — table changes never move latency).
//   2. Input finiteness scan: a non-finite host buffer is scrubbed to silence BEFORE it can
//      poison the envelope follower, the convolver history, or the dry delay.
//   3. ModulationEngine.beginBlock(transport, notes, mono tap = 0.5*(L+R), n).
//   4. The block is chopped at ABSOLUTE control ticks (EngineConfig::controlInterval samples
//      of the *stream*, tracked by a running sample counter, so identical sample streams
//      produce bit-identical output for any block chopping). Per tick: mod.evaluate ->
//      effective targets (scan/cutoff/resonance, clamped after summing) -> one-pole smoothers
//      (~15 ms, cutoff in log2), mix/out-gain ramp retargets, the mode-switch state machine,
//      and at kernel ticks (EngineConfig::kernelUpdateInterval, a multiple of the control
//      tick) the change detector -> KernelGenerator::generate INLINE (RT-safe by A2's
//      contract) -> ConvolutionSection::pushKernel (per-sample crossfade, one kernel tick
//      long) -> ResponseCurve publication.
//   5. Wet = section.process on a scratch copy; dry = the original input through a ring
//      delay of exactly the reported latency; out = (dry + mix*(modeFade*wet - dry)) * gain,
//      with mix/gain per-sample linear ramps (~10 ms) and modeFade the 5 ms mode-switch fade.
//   6. Mode switch (params.mode != active mode): 5 ms wet fade-out -> setKernelImmediate(new
//      mode kernel) (this also resets convolver state) + dry-delay length switch -> 5 ms wet
//      fade-in. latencySamples() reflects the new mode from the tick the switch is REQUESTED.
//   7. Robustness: per-segment wet finiteness check (on NaN/inf: reset section, silence, keep
//      going); silence idle: after > L + one kernel tick of input below -180 dBFS the state
//      is cleared once and zeros are emitted cheaply until signal returns (param/mode changes
//      still tracked so the resume is fresh).
//
// ResponseCurve construction: A2 exposes no response hook, so the engine FFTs the freshly
// generated kernel (one preallocated 2L-point real FFT at kernel builds only) and samples the
// magnitude at the 256 ResponseCurve grid points — this measures the TRUE kernel including
// windowing, and is the cheapest correct option (the kernel is already in hand).
//
// Thread contract: see ftc/FilterTableEngine.h. prepare() builds the initial kernel
// synchronously (adopting any table published before prepare) so the first block is valid.
#include "ftc/FilterTableEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "ftc/Convolver.h"
#include "ftc/Denormals.h"
#include "ftc/EngineConfig.h"
#include "ftc/FFT.h"
#include "ftc/Kernel.h"
#include "ftc/KernelGenerator.h"
#include "ftc/Modulation.h"
#include "ftc/RealtimeExchange.h"

namespace ftc {

namespace {

constexpr int kCtrl = EngineConfig::controlInterval;
constexpr float kSilenceThresh = 1e-9f; // -180 dBFS
constexpr int kMaxChunkNotes = 512;     // note events forwarded per internal chunk

inline float clamp01f(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline int roundUpToTicks(int samples) noexcept {
    const int t = (samples + kCtrl - 1) / kCtrl;
    return (t < 1 ? 1 : t) * kCtrl;
}

inline float dbToLinear(float db) noexcept {
    return std::pow(10.0f, db * (1.0f / 20.0f));
}

} // namespace

struct FilterTableEngine::Impl {
    // ------------------------------------------------------------------ config
    PrepareSpec spec{};
    bool prepared = false;
    double fs = 48000.0;
    int L = 2048;          // kernel taps for this fs tier
    int kernelTick = 128;  // kernel rebuild cadence (multiple of kCtrl)
    int maxBlock = 512;
    int chans = 2;

    // -------------------------------------------------------------- components
    Parameters params{};
    KernelGenerator generator;
    ConvolutionSection section;
    ModulationEngine mod;
    Kernel kernelScratch;
    ObjectHandoff<WavetableData> handoff;
    const WavetableData* activeTable = nullptr;
    WavetablePtr uiTable; // message-thread mirror for the GUI

    // ----------------------------------------------------------------- UI taps
    TripleBuffer<ResponseCurve> curveBuf;
    ResponseCurve curveScratch{};
    std::atomic<float> scanAtomic{0.0f};
    std::atomic<float> envAtomic{0.0f};
    std::atomic<int> latencyAtomic{0};

    // Response-curve probe: FFT of the freshly built kernel, sampled on the frozen grid.
    std::optional<RealFFT> curveFft; // 2L-point
    AlignedVector<float> curveTime;
    AlignedVector<std::complex<float>> curveSpec;
    std::array<int, ResponseCurve::kNumPoints> curveBin{};
    std::array<float, ResponseCurve::kNumPoints> curveFrac{};

    // --------------------------------------------------------------- scratches
    AlignedVector<float> mono;                 // mod input tap, maxBlock samples
    std::vector<AlignedVector<float>> wet;     // per channel, one control tick
    std::vector<float*> wetPtrs;
    std::vector<float*> chanScratch;           // offset host pointers for oversize blocks
    std::vector<NoteEvent> noteScratch;        // rebased notes for oversize blocks

    // --------------------------------------------------------------- dry delay
    std::vector<AlignedVector<float>> delayBuf; // per channel, pow2 ring of L samples
    int delaySize = 0;
    int delayMask = 0;
    int delayWrite = 0;
    int delaySamples = 0; // == reported latency of the ACTIVE mode

    // ------------------------------------------------------------ stream state
    std::uint64_t globalPos = 0; // absolute sample counter -> tick grid
    bool snapSmoothers = true;   // first tick after prepare/reset snaps to targets
    double smoothCoeff = 0.085;  // per-tick one-pole coefficient (~15 ms)
    double scanS = 0.0;          // smoothed effective scan 0..1
    double cutoffLog2S = 8.781;  // smoothed effective cutoff, log2(Hz)
    double resS = 0.0;           // smoothed effective resonance -1..1

    // Change detector: request values of the most recently built kernel.
    double lastScan = 0.0;
    double lastLog2 = 0.0;
    double lastRes = 0.0;
    bool kernelDirty = true;

    // Mode-switch state machine (all transitions on the tick grid).
    enum class Switch { Stable, FadeOut, FadeIn };
    Switch switchState = Switch::Stable;
    PhaseMode activeMode = PhaseMode::Minimum;  // kernels currently in the section
    PhaseMode pendingMode = PhaseMode::Minimum; // requested mode while fading
    int modeFadeLen = 256;                      // 5 ms rounded up to whole ticks
    int modeFadePos = 256;                      // 0 = silent wet, modeFadeLen = full wet
    float invModeFadeLen = 1.0f / 256.0f;
    // After the hard swap the (reset) convolver outputs silence for the new mode's latency;
    // the fade-in holds at zero for that long so it ramps over ARRIVING signal, not ahead of
    // it (otherwise the delayed signal's onset would land after the fade already completed).
    int modeFadeHold = 0;

    // Per-sample linear ramps (~10 ms) for mix and output gain.
    int rampLen = 480;
    float mixCur = 1.0f, mixTarget = 1.0f, mixStep = 0.0f;
    int mixLeft = 0;
    float gainCur = 1.0f, gainTarget = 1.0f, gainStep = 0.0f;
    int gainLeft = 0;
    float lastGainDb = 0.0f;

    // Silence idle.
    std::uint64_t silentRun = 0;
    bool idle = false;

    // ------------------------------------------------------------------ helpers
    int latencyFor(PhaseMode m) const noexcept {
        return KernelGenerator::latencyForMode(m, L);
    }

    double maxCutoffHz() const noexcept {
        return std::min(EngineConfig::maxCutoffHz, EngineConfig::maxCutoffFsFraction * fs);
    }

    KernelRequest currentRequest() const noexcept {
        KernelRequest r;
        r.mode = activeMode;
        r.scan = static_cast<float>(scanS);
        r.cutoffHz = static_cast<float>(std::exp2(cutoffLog2S));
        r.resonance = static_cast<float>(resS);
        return r;
    }

    /// Record the smoothed values the just-built kernel was generated from.
    void noteBuilt() noexcept {
        lastScan = scanS;
        lastLog2 = cutoffLog2S;
        lastRes = resS;
        kernelDirty = false;
    }

    bool requestDrifted() const noexcept {
        return kernelDirty
               || std::abs(scanS - lastScan)
                      > static_cast<double>(EngineConfig::scanDeltaThreshold)
               || std::abs(cutoffLog2S - lastLog2)
                      > static_cast<double>(EngineConfig::cutoffLog2DeltaThreshold)
               || std::abs(resS - lastRes)
                      > static_cast<double>(EngineConfig::resonanceDeltaThreshold);
    }

    /// FFT the freshly generated kernel and publish its magnitude on the frozen UI grid.
    void publishCurve() noexcept {
        const int len = kernelScratch.length();
        const int n = curveFft->size();
        if (len <= 0 || len > n)
            return;
        std::memcpy(curveTime.data(), kernelScratch.data(),
                    sizeof(float) * static_cast<size_t>(len));
        std::memset(curveTime.data() + len, 0, sizeof(float) * static_cast<size_t>(n - len));
        curveFft->forward(curveTime.data(), curveSpec.data());
        for (int i = 0; i < ResponseCurve::kNumPoints; ++i) {
            const int b = curveBin[static_cast<size_t>(i)];
            const float f = curveFrac[static_cast<size_t>(i)];
            const float m0 = std::abs(curveSpec[static_cast<size_t>(b)]);
            const float m1 = std::abs(curveSpec[static_cast<size_t>(b + 1)]);
            const float mag = m0 + f * (m1 - m0);
            curveScratch.db[static_cast<size_t>(i)] =
                20.0f * std::log10(std::max(mag, EngineConfig::floorLinear));
        }
        curveBuf.write(curveScratch);
    }

    /// Build a kernel for the current smoothed request and hard-swap it in (prepare, mode
    /// swaps, idle resume — never during normal streaming, which crossfades via pushKernel).
    void rebuildImmediate() noexcept {
        const KernelRequest r = currentRequest();
        generator.generate(r, kernelScratch);
        section.setKernelImmediate(kernelScratch);
        noteBuilt();
        publishCurve();
    }

    // ------------------------------------------------------------------ prepare
    void prepare(const PrepareSpec& s) {
        spec = s;
        fs = s.sampleRate > 1.0 ? s.sampleRate : 48000.0;
        L = EngineConfig::kernelLength(fs);
        kernelTick = EngineConfig::kernelUpdateInterval(fs);
        maxBlock = s.maxBlockSize > 0 ? s.maxBlockSize : 1;
        chans = s.numChannels > 0 ? s.numChannels : 1;

        generator.prepare({fs, L});

        PartitionedConvolver::Config cc;
        cc.maxKernelLength = L;
        cc.headLength = EngineConfig::headLength;
        cc.partitionLength = EngineConfig::partitionLength;
        cc.maxBlockSize = std::max(maxBlock, kCtrl);
        section.prepare(cc, chans, kernelTick);

        mod.prepare(fs, kCtrl);
        kernelScratch.prepare(L);

        // Response-curve probe grid (frozen mapping in ftc/ResponseCurve.h).
        curveFft.emplace(2 * L);
        curveTime.assign(static_cast<size_t>(2 * L), 0.0f);
        curveSpec.assign(static_cast<size_t>(L + 1), std::complex<float>{});
        for (int i = 0; i < ResponseCurve::kNumPoints; ++i) {
            const double f = static_cast<double>(ResponseCurve::frequencyForPoint(i));
            double bp = f * static_cast<double>(2 * L) / fs;
            bp = std::clamp(bp, 0.0, static_cast<double>(L - 1));
            const int b = static_cast<int>(bp);
            curveBin[static_cast<size_t>(i)] = b;
            curveFrac[static_cast<size_t>(i)] = static_cast<float>(bp - static_cast<double>(b));
        }

        mono.assign(static_cast<size_t>(maxBlock), 0.0f);
        wet.assign(static_cast<size_t>(chans), {});
        wetPtrs.assign(static_cast<size_t>(chans), nullptr);
        for (int c = 0; c < chans; ++c) {
            wet[static_cast<size_t>(c)].assign(static_cast<size_t>(kCtrl), 0.0f);
            wetPtrs[static_cast<size_t>(c)] = wet[static_cast<size_t>(c)].data();
        }
        chanScratch.assign(static_cast<size_t>(chans), nullptr);
        noteScratch.assign(static_cast<size_t>(kMaxChunkNotes), NoteEvent{});

        delaySize = L; // pow2 and > max latency (L/2)
        delayMask = delaySize - 1;
        delayWrite = 0;
        delayBuf.assign(static_cast<size_t>(chans), {});
        for (int c = 0; c < chans; ++c)
            delayBuf[static_cast<size_t>(c)].assign(static_cast<size_t>(delaySize), 0.0f);

        // Timing constants.
        smoothCoeff = 1.0 - std::exp(-static_cast<double>(kCtrl)
                                     / (EngineConfig::scanSmoothSeconds * fs));
        rampLen = std::max(1, static_cast<int>(EngineConfig::mixRampSeconds * fs + 0.5));
        modeFadeLen = roundUpToTicks(
            static_cast<int>(EngineConfig::modeSwitchFadeSeconds * fs + 0.5));
        invModeFadeLen = 1.0f / static_cast<float>(modeFadeLen);
        modeFadePos = modeFadeLen;
        modeFadeHold = 0;

        // Seed control state from the current parameter snapshot (zero modulation).
        const float scan0 = std::isfinite(params.scan) ? clamp01f(params.scan) : 0.0f;
        double fc0 = std::isfinite(params.cutoffHz) ? static_cast<double>(params.cutoffHz)
                                                    : 440.0;
        fc0 = std::clamp(fc0, EngineConfig::minCutoffHz, maxCutoffHz());
        const float res0 =
            std::isfinite(params.resonance) ? std::clamp(params.resonance, -1.0f, 1.0f) : 0.0f;
        scanS = static_cast<double>(scan0);
        cutoffLog2S = std::log2(fc0);
        resS = static_cast<double>(res0);
        snapSmoothers = true;

        mixTarget = std::isfinite(params.mix) ? clamp01f(params.mix) : 1.0f;
        mixCur = mixTarget;
        mixStep = 0.0f;
        mixLeft = 0;
        lastGainDb =
            std::isfinite(params.outGainDb) ? std::clamp(params.outGainDb, -24.0f, 12.0f) : 0.0f;
        gainTarget = dbToLinear(lastGainDb);
        gainCur = gainTarget;
        gainStep = 0.0f;
        gainLeft = 0;

        activeMode = params.mode;
        pendingMode = activeMode;
        switchState = Switch::Stable;
        delaySamples = latencyFor(activeMode);
        latencyAtomic.store(delaySamples, std::memory_order_relaxed);

        globalPos = 0;
        silentRun = 0;
        idle = false;

        // Adopt any table published before prepare and build the initial kernel
        // synchronously so the very first block is valid (host serializes prepare/process).
        activeTable = handoff.acquire();
        generator.setWavetable(activeTable);
        rebuildImmediate();

        scanAtomic.store(static_cast<float>(scanS), std::memory_order_relaxed);
        envAtomic.store(0.0f, std::memory_order_relaxed);
        prepared = true;
    }

    // -------------------------------------------------------------------- reset
    void reset() noexcept {
        if (!prepared)
            return;
        section.reset();
        mod.reset();
        for (auto& d : delayBuf)
            std::memset(d.data(), 0, sizeof(float) * static_cast<size_t>(delaySize));
        delayWrite = 0;
        globalPos = 0;
        snapSmoothers = true;
        silentRun = 0;
        idle = false;
        switchState = Switch::Stable;
        pendingMode = activeMode;
        modeFadePos = modeFadeLen;
        modeFadeHold = 0;
        mixCur = mixTarget;
        mixStep = 0.0f;
        mixLeft = 0;
        gainCur = gainTarget;
        gainStep = 0.0f;
        gainLeft = 0;
        // Kernels, latency, and the published curve are kept (facade contract).
    }

    // --------------------------------------------------------------- idle paths
    void enterIdle() noexcept {
        idle = true;
        section.reset();
        for (auto& d : delayBuf)
            std::memset(d.data(), 0, sizeof(float) * static_cast<size_t>(delaySize));
        delayWrite = 0;
        mixCur = mixTarget;
        mixStep = 0.0f;
        mixLeft = 0;
        gainCur = gainTarget;
        gainStep = 0.0f;
        gainLeft = 0;
        if (switchState != Switch::Stable) { // finish any in-flight switch silently
            activeMode = pendingMode;
            delaySamples = latencyFor(activeMode);
            kernelDirty = true;
            switchState = Switch::Stable;
            modeFadePos = modeFadeLen;
            modeFadeHold = 0;
        }
    }

    void resumeFromIdle() noexcept {
        idle = false;
        silentRun = 0;
        if (requestDrifted())
            rebuildImmediate(); // hard swap is inaudible coming out of silence
    }

    // ------------------------------------------------------------- control tick
    void controlTick(int blockOffset) noexcept {
        const ModValues mv = mod.evaluate(blockOffset);

        // Effective targets: clamp AFTER summing (docs/INTERFACES.md scaling contracts).
        float scanT = params.scan + mv.scanOffset;
        if (!std::isfinite(scanT))
            scanT = static_cast<float>(scanS);
        scanT = clamp01f(scanT);

        const float semis = std::isfinite(mv.cutoffSemis) ? mv.cutoffSemis : 0.0f;
        double fcT = static_cast<double>(params.cutoffHz)
                     * std::exp2(static_cast<double>(semis) / 12.0);
        if (!std::isfinite(fcT))
            fcT = std::exp2(cutoffLog2S);
        fcT = std::clamp(fcT, EngineConfig::minCutoffHz, maxCutoffHz());
        const double log2T = std::log2(fcT);

        float resT = std::isfinite(params.resonance)
                         ? std::clamp(params.resonance, -1.0f, 1.0f)
                         : static_cast<float>(resS);

        if (snapSmoothers) {
            scanS = static_cast<double>(scanT);
            cutoffLog2S = log2T;
            resS = static_cast<double>(resT);
            snapSmoothers = false;
        } else {
            scanS += smoothCoeff * (static_cast<double>(scanT) - scanS);
            cutoffLog2S += smoothCoeff * (log2T - cutoffLog2S);
            resS += smoothCoeff * (static_cast<double>(resT) - resS);
        }

        // Mix / output-gain ramp targets (per-sample linear ramps, retargeted on the grid).
        float mixT = std::isfinite(params.mix) ? clamp01f(params.mix) : mixTarget;
        if (mixT != mixTarget) {
            mixTarget = mixT;
            mixStep = (mixTarget - mixCur) / static_cast<float>(rampLen);
            mixLeft = rampLen;
        }
        const float gDb = std::isfinite(params.outGainDb)
                              ? std::clamp(params.outGainDb, -24.0f, 12.0f)
                              : lastGainDb;
        if (gDb != lastGainDb) {
            lastGainDb = gDb;
            gainTarget = dbToLinear(gDb);
            gainStep = (gainTarget - gainCur) / static_cast<float>(rampLen);
            gainLeft = rampLen;
        }
        if (idle) { // silent: snap ramps so a resume starts at the requested values
            mixCur = mixTarget;
            mixStep = 0.0f;
            mixLeft = 0;
            gainCur = gainTarget;
            gainStep = 0.0f;
            gainLeft = 0;
        }

        // Mode-switch state machine (latency is reported from the REQUEST tick).
        if (idle) {
            if (params.mode != activeMode) {
                activeMode = params.mode;
                pendingMode = params.mode;
                delaySamples = latencyFor(activeMode);
                latencyAtomic.store(delaySamples, std::memory_order_relaxed);
                kernelDirty = true;
            }
        } else {
            switch (switchState) {
                case Switch::Stable:
                    if (params.mode != activeMode) {
                        pendingMode = params.mode;
                        latencyAtomic.store(latencyFor(pendingMode), std::memory_order_relaxed);
                        switchState = Switch::FadeOut;
                    }
                    break;
                case Switch::FadeOut:
                    if (params.mode != pendingMode) {
                        pendingMode = params.mode;
                        latencyAtomic.store(latencyFor(pendingMode), std::memory_order_relaxed);
                    }
                    if (pendingMode == activeMode) {
                        switchState = Switch::FadeIn; // canceled — ramp straight back up
                        modeFadeHold = 0;
                    } else if (modeFadePos == 0) {
                        activeMode = pendingMode;
                        delaySamples = latencyFor(activeMode);
                        rebuildImmediate(); // hard swap + convolver reset, masked by the fade
                        modeFadeHold = delaySamples; // wet pipeline fill time
                        switchState = Switch::FadeIn;
                    }
                    break;
                case Switch::FadeIn:
                    if (params.mode != activeMode) {
                        pendingMode = params.mode;
                        latencyAtomic.store(latencyFor(pendingMode), std::memory_order_relaxed);
                        switchState = Switch::FadeOut;
                    } else if (modeFadePos >= modeFadeLen) {
                        switchState = Switch::Stable;
                    }
                    break;
            }
        }

        // Kernel tick: change detector -> inline generate -> crossfade push.
        if (!idle && switchState == Switch::Stable
            && globalPos % static_cast<std::uint64_t>(kernelTick) == 0 && requestDrifted()
            && !section.isFading()) {
            const KernelRequest r = currentRequest();
            generator.generate(r, kernelScratch);
            if (section.pushKernel(kernelScratch)) {
                noteBuilt();
                publishCurve();
            }
        }

        // Silence idle entry, decided on the grid (input has been quiet long enough that all
        // internal state has decayed to exact zeros — kernel length + one tick).
        if (!idle && silentRun > static_cast<std::uint64_t>(L + kernelTick))
            enterIdle();

        scanAtomic.store(static_cast<float>(scanS), std::memory_order_relaxed);
        envAtomic.store(mod.envValue(), std::memory_order_relaxed);
    }

    // ------------------------------------------------------------ chunk process
    /// One internal chunk (n <= maxBlock). `chs` are already offset; `live` <= chans.
    void processChunk(float* const* chs, int live, int n, const TransportInfo& transport,
                      std::span<const NoteEvent> notes) noexcept {
        // Input finiteness scrub: never let NaN/inf reach mod, the section, or the delay.
        float acc = 0.0f;
        for (int c = 0; c < live; ++c) {
            const float* ch = chs[c];
            for (int i = 0; i < n; ++i)
                acc += ch[i];
        }
        if (!std::isfinite(acc)) {
            for (int c = 0; c < live; ++c)
                std::memset(chs[c], 0, sizeof(float) * static_cast<size_t>(n));
        }

        // Mono tap for the envelope follower.
        if (live >= 2) {
            const float* a = chs[0];
            const float* b = chs[1];
            for (int i = 0; i < n; ++i)
                mono[static_cast<size_t>(i)] = 0.5f * (a[i] + b[i]);
        } else {
            std::memcpy(mono.data(), chs[0], sizeof(float) * static_cast<size_t>(n));
        }

        mod.setParams(params);
        mod.beginBlock(transport, notes, mono.data(), n);

        int pos = 0;
        while (pos < n) {
            const int into = static_cast<int>(globalPos % static_cast<std::uint64_t>(kCtrl));
            if (into == 0)
                controlTick(pos);
            const int seg = std::min(n - pos, kCtrl - into);

            // Segment input peak (idle tracking / resume detection).
            float peak = 0.0f;
            for (int c = 0; c < live; ++c) {
                const float* ch = chs[c] + pos;
                for (int i = 0; i < seg; ++i)
                    peak = std::max(peak, std::abs(ch[i]));
            }

            if (idle) {
                if (peak <= kSilenceThresh) { // stay idle: emit zeros, skip all DSP
                    for (int c = 0; c < live; ++c)
                        std::memset(chs[c] + pos, 0, sizeof(float) * static_cast<size_t>(seg));
                    silentRun += static_cast<std::uint64_t>(seg);
                    globalPos += static_cast<std::uint64_t>(seg);
                    pos += seg;
                    continue;
                }
                resumeFromIdle();
            }
            silentRun = peak <= kSilenceThresh ? silentRun + static_cast<std::uint64_t>(seg) : 0;

            // Wet path on a scratch copy (section processes in place).
            for (int c = 0; c < chans; ++c) {
                if (c < live)
                    std::memcpy(wet[static_cast<size_t>(c)].data(), chs[c] + pos,
                                sizeof(float) * static_cast<size_t>(seg));
                else
                    std::memset(wet[static_cast<size_t>(c)].data(), 0,
                                sizeof(float) * static_cast<size_t>(seg));
            }
            section.process(wetPtrs.data(), seg);

            // Wet finiteness: on NaN/inf reset the section and silence this segment (the dry
            // input is known finite; state recovers on the next segment).
            float wacc = 0.0f;
            for (int c = 0; c < live; ++c) {
                const float* w = wet[static_cast<size_t>(c)].data();
                for (int i = 0; i < seg; ++i)
                    wacc += w[i];
            }
            const bool badWet = !std::isfinite(wacc);
            if (badWet)
                section.reset();

            // Per-sample mixing: ramps advance once per sample, identically for any chopping.
            const bool fadingOut = switchState == Switch::FadeOut;
            const bool fadingIn = switchState == Switch::FadeIn;
            for (int i = 0; i < seg; ++i) {
                if (mixLeft > 0) {
                    mixCur += mixStep;
                    --mixLeft;
                } else {
                    mixCur = mixTarget;
                }
                if (gainLeft > 0) {
                    gainCur += gainStep;
                    --gainLeft;
                } else {
                    gainCur = gainTarget;
                }
                if (fadingOut) {
                    if (modeFadePos > 0)
                        --modeFadePos;
                } else if (fadingIn) {
                    if (modeFadeHold > 0)
                        --modeFadeHold;
                    else if (modeFadePos < modeFadeLen)
                        ++modeFadePos;
                }
                const float fade = modeFadePos >= modeFadeLen ? 1.0f
                                   : modeFadePos <= 0
                                       ? 0.0f
                                       : static_cast<float>(modeFadePos) * invModeFadeLen;
                for (int c = 0; c < live; ++c) {
                    float* ch = chs[c] + pos;
                    const float x = ch[i];
                    float* dbuf = delayBuf[static_cast<size_t>(c)].data();
                    dbuf[delayWrite] = x;
                    const float dry =
                        dbuf[(delayWrite - delaySamples + delaySize) & delayMask];
                    const float wetS = fade * wet[static_cast<size_t>(c)][static_cast<size_t>(i)];
                    const float y = (dry + mixCur * (wetS - dry)) * gainCur;
                    ch[i] = badWet ? 0.0f : y;
                }
                delayWrite = (delayWrite + 1) & delayMask;
            }

            globalPos += static_cast<std::uint64_t>(seg);
            pos += seg;
        }
    }

    // ------------------------------------------------------------ block process
    void process(float* const* channels, int numChannels, int numSamples,
                 const TransportInfo& transport, std::span<const NoteEvent> notes) noexcept {
        if (numChannels <= 0 || numSamples <= 0)
            return;
        if (!prepared) { // facade contract: silence until prepared
            for (int c = 0; c < numChannels; ++c)
                std::memset(channels[c], 0, sizeof(float) * static_cast<size_t>(numSamples));
            return;
        }

        ScopedNoDenormals noDenormals;

        // Adopt any pending wavetable at block start; a change is a normal kernel rebuild.
        const WavetableData* t = handoff.acquire();
        if (t != activeTable) {
            activeTable = t;
            generator.setWavetable(t);
            kernelDirty = true;
        }

        const int live = std::min(numChannels, chans);
        for (int c = live; c < numChannels; ++c) // channels beyond the prepared count
            std::memset(channels[c], 0, sizeof(float) * static_cast<size_t>(numSamples));

        if (numSamples <= maxBlock) { // fast path: the host honors its declared maximum
            processChunk(channels, live, numSamples, transport, notes);
            return;
        }

        // Defensive slicing for hosts that exceed maxBlockSize: split into chunks, rebasing
        // note offsets and advancing the transport snapshot per chunk.
        int done = 0;
        while (done < numSamples) {
            const int len = std::min(numSamples - done, maxBlock);
            for (int c = 0; c < live; ++c)
                chanScratch[static_cast<size_t>(c)] = channels[c] + done;
            TransportInfo tr = transport;
            if (tr.valid && done > 0)
                tr.ppqPosition += static_cast<double>(done) * tr.bpm / (60.0 * fs);
            int cnt = 0;
            for (const NoteEvent& e : notes) {
                if (e.sampleOffset >= done && e.sampleOffset < done + len
                    && cnt < kMaxChunkNotes) {
                    noteScratch[static_cast<size_t>(cnt)] = e;
                    noteScratch[static_cast<size_t>(cnt)].sampleOffset -= done;
                    ++cnt;
                }
            }
            processChunk(chanScratch.data(), live, len, tr,
                         std::span<const NoteEvent>(noteScratch.data(),
                                                    static_cast<size_t>(cnt)));
            done += len;
        }
    }
};

// --------------------------------------------------------------------- facade
FilterTableEngine::FilterTableEngine() : impl_(std::make_unique<Impl>()) {}
FilterTableEngine::~FilterTableEngine() = default;

void FilterTableEngine::prepare(const PrepareSpec& spec) {
    impl_->prepare(spec);
}

void FilterTableEngine::reset() noexcept {
    impl_->reset();
}

void FilterTableEngine::setParameters(const Parameters& params) noexcept {
    impl_->params = params;
}

void FilterTableEngine::setWavetable(WavetablePtr table) {
    impl_->uiTable = table;
    impl_->handoff.publish(std::move(table));
}

void FilterTableEngine::process(float* const* channels, int numChannels, int numSamples,
                                const TransportInfo& transport,
                                std::span<const NoteEvent> notes) noexcept {
    impl_->process(channels, numChannels, numSamples, transport, notes);
}

int FilterTableEngine::latencySamples() const noexcept {
    return impl_->latencyAtomic.load(std::memory_order_relaxed);
}

int FilterTableEngine::latencySamplesFor(PhaseMode mode, double sampleRate) noexcept {
    return KernelGenerator::latencyForMode(mode, EngineConfig::kernelLength(sampleRate));
}

void FilterTableEngine::collectGarbage() {
    impl_->handoff.collectGarbage();
}

bool FilterTableEngine::readResponseCurve(ResponseCurve& out) noexcept {
    return impl_->curveBuf.read(out);
}

float FilterTableEngine::currentScan() const noexcept {
    return impl_->scanAtomic.load(std::memory_order_relaxed);
}

float FilterTableEngine::envValue() const noexcept {
    return impl_->envAtomic.load(std::memory_order_relaxed);
}

WavetablePtr FilterTableEngine::currentTableForUi() const {
    return impl_->uiTable;
}

} // namespace ftc
