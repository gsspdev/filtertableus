// FilterTableUS core — ModulationEngine: two LFOs (timeline-locked, tempo-syncable, retrigger,
// deterministic S&H), an envelope follower, and a last-note tracker. Replaces mod_stub.cpp.
//
// Threading (per ftc/Modulation.h): prepare() allocates; setParams/beginBlock/evaluate/envValue
// are audio-thread-only and RT-safe (no allocation, locks, or exceptions after prepare()).
//
// Design notes:
//  - evaluate(offset) is called at control ticks with non-decreasing offsets inside a block.
//    Note events are consumed lazily up to each evaluate offset, so retrigger and keytrack are
//    applied with sample-offset ordering at control-tick granularity. Any events after the last
//    evaluate of a block are consumed at the start of the next beginBlock().
//  - Timeline lock: while the host plays, LFO phase is a pure function of the transport
//    (ppq for tempo-sync; ppq-derived timeline seconds for free-rate), so loop jumps land on
//    identical values. When stopped/invalid, phase free-runs from the current BPM/rate,
//    continuing from the last computed phase.
//  - S&H: while playing, the value is a deterministic hash of (seed, cycle index), so looping
//    the timeline repeats the same randoms. When stopped, values draw sequentially from a
//    seeded RNG on each cycle wrap. The seed is derived from a fixed per-LFO base and a
//    play-start counter ("reseed on transport start"); each retrigger note also perturbs the
//    seed so successive notes get fresh randoms.
//  - Envelope follower: the block's rectified mono input is consumed eagerly in beginBlock()
//    (the input pointer is NOT retained); the state at each control-tick boundary is
//    snapshotted so evaluate(offset) reads the follower "at" that offset.
#include "ftc/Modulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace ftc {

namespace {

constexpr int kMaxBlockNotes = 512;      // note events kept per block (extras are dropped)
constexpr int kMaxSnapshotSamples = 65536; // env snapshots cover blocks up to this length
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kDefaultBpm = 120.0;
constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ULL;
constexpr std::int64_t kCycleSentinel = std::numeric_limits<std::int64_t>::min();

inline double fracPart(double x) noexcept { return x - std::floor(x); }

inline std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += kGolden;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/// Top 24 bits -> bipolar float in [-1, 1).
inline float bipolarFromBits(std::uint64_t bits) noexcept {
    const double u = static_cast<double>(bits >> 40) * (1.0 / 16777216.0);
    return static_cast<float>(2.0 * u - 1.0);
}

/// Bipolar shape value for a wrapped phase in [0, 1). Sine-aligned conventions:
/// sine/triangle start at 0 rising (+1 at 1/4 cycle), square is +1 for the first half.
inline float lfoShapeValue(LfoShape shape, double phase, float sah) noexcept {
    switch (shape) {
        case LfoShape::Sine: return static_cast<float>(std::sin(kTwoPi * phase));
        case LfoShape::Triangle:
            if (phase < 0.25) return static_cast<float>(4.0 * phase);
            if (phase < 0.75) return static_cast<float>(2.0 - 4.0 * phase);
            return static_cast<float>(4.0 * phase - 4.0);
        case LfoShape::SawUp: return static_cast<float>(2.0 * phase - 1.0);
        case LfoShape::SawDown: return static_cast<float>(1.0 - 2.0 * phase);
        case LfoShape::Square: return phase < 0.5 ? 1.0f : -1.0f;
        case LfoShape::SampleHold: return sah;
    }
    return 0.0f;
}

inline float clamp01(double v) noexcept {
    return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

struct LfoState {
    double freePhase = 0.0;        // unwrapped cycles while free-running (transport stopped)
    double lastWrappedPhase = 0.0; // last computed phase (play->stop continuity)
    double anchorPpq = 0.0;        // retrigger anchor while playing, tempo-synced (quarters)
    double anchorSec = 0.0;        // retrigger anchor while playing, free-rate (timeline sec)
    std::uint64_t seed = 0;        // S&H hash seed (playing)
    std::uint64_t rng = 0;         // S&H sequential RNG (stopped)
    float sah = 0.0f;
    std::int64_t lastCycle = kCycleSentinel;
};

} // namespace

struct ModulationEngine::Impl {
    double fs = 48000.0;
    double invFs = 1.0 / 48000.0;
    int tick = 64;
    Parameters params{};

    // --- current block context -------------------------------------------------------------
    TransportInfo transport{};
    bool playing = false;
    double bpm = kDefaultBpm;
    double beatsPerSample = kDefaultBpm / (60.0 * 48000.0);
    std::array<NoteEvent, kMaxBlockNotes> notes{};
    int numNotes = 0;
    int noteCursor = 0;
    int blockLen = 0;
    int evalCursor = 0; // furthest sample offset already accounted for in this block
    bool haveBlock = false;

    // --- envelope follower -----------------------------------------------------------------
    double envState = 0.0;
    float envOut = 0.0f;
    std::vector<float> envSnap; // envSnap[k] = clamped state after k*tick samples of the block
    int envSnapCount = 1;

    // --- note tracker: held-note stack, last-note priority, hold after all-off --------------
    std::array<std::uint8_t, 128> held{};
    int heldCount = 0;
    int lastNote = 69; // A4 -> keytrack contributes 0 until the first note arrives

    // --- transport bookkeeping ---------------------------------------------------------------
    bool wasPlaying = false;
    std::uint64_t playCount = 0;

    LfoState lfo[2];

    const LfoParams& lfoParams(int i) const noexcept { return i == 0 ? params.lfo1 : params.lfo2; }

    void reseedLfo(int i) noexcept {
        // Fixed per-LFO base mixed with the play-start counter: deterministic run-to-run,
        // fresh sequence on every transport start.
        const std::uint64_t base = 0x46744C664F310000ULL + static_cast<std::uint64_t>(i) * 7919u;
        lfo[i].seed = splitmix64(base + playCount * kGolden);
        lfo[i].rng = splitmix64(lfo[i].seed ^ 0x5DEECE66DULL);
    }

    double phaseIncPerSample(const LfoParams& lp) const noexcept {
        if (lp.tempoSync) {
            const double bpc = beatsPerCycle(lp.division);
            return bpc > 0.0 ? beatsPerSample / bpc : 0.0;
        }
        return std::max(0.0, static_cast<double>(lp.rateHz)) * invFs;
    }

    double timelineSecAt(int offset) const noexcept {
        return transport.ppqPosition * 60.0 / bpm + static_cast<double>(offset) * invFs;
    }

    void pushHeld(std::uint8_t note) noexcept {
        removeHeld(note); // re-striking a held note moves it to the top
        if (heldCount < static_cast<int>(held.size()))
            held[static_cast<size_t>(heldCount++)] = note;
    }

    void removeHeld(std::uint8_t note) noexcept {
        for (int k = 0; k < heldCount; ++k) {
            if (held[static_cast<size_t>(k)] == note) {
                for (int m = k; m < heldCount - 1; ++m)
                    held[static_cast<size_t>(m)] = held[static_cast<size_t>(m + 1)];
                --heldCount;
                return;
            }
        }
    }

    /// Consume note events with sampleOffset <= offset and advance free-running phases.
    /// Monotonic within a block; idempotent for repeated offsets.
    void advanceTo(int offset) noexcept {
        if (offset < evalCursor)
            return;
        int retrigAt[2] = {-1, -1};
        while (noteCursor < numNotes &&
               notes[static_cast<size_t>(noteCursor)].sampleOffset <= offset) {
            const NoteEvent& e = notes[static_cast<size_t>(noteCursor)];
            ++noteCursor;
            if (e.noteOn) {
                pushHeld(e.note);
                lastNote = e.note;
                if (params.lfo1.retrigger) retrigAt[0] = e.sampleOffset;
                if (params.lfo2.retrigger) retrigAt[1] = e.sampleOffset;
            } else {
                removeHeld(e.note);
                if (heldCount > 0)
                    lastNote = held[static_cast<size_t>(heldCount - 1)]; // last-note priority
                // all notes off: hold the last value
            }
        }
        for (int i = 0; i < 2; ++i) {
            LfoState& L = lfo[i];
            const double inc = phaseIncPerSample(lfoParams(i));
            if (retrigAt[i] >= 0) {
                L.seed = splitmix64(L.seed + kGolden); // fresh S&H randomness per note
                L.lastCycle = kCycleSentinel;          // force an S&H draw at the next value
                if (playing) {
                    L.anchorPpq = transport.ppqPosition +
                                  static_cast<double>(retrigAt[i]) * beatsPerSample;
                    L.anchorSec = timelineSecAt(retrigAt[i]);
                } else {
                    L.freePhase = static_cast<double>(offset - retrigAt[i]) * inc;
                }
            } else if (!playing) {
                L.freePhase += static_cast<double>(offset - evalCursor) * inc;
            }
        }
        evalCursor = offset;
    }

    float lfoValueAt(int i, int offset) noexcept {
        const LfoParams& lp = lfoParams(i);
        LfoState& L = lfo[i];
        double u; // unwrapped cycle position
        if (playing) {
            if (lp.tempoSync) {
                const double bpc = beatsPerCycle(lp.division);
                const double ppqNow =
                    transport.ppqPosition + static_cast<double>(offset) * beatsPerSample;
                u = bpc > 0.0 ? (ppqNow - L.anchorPpq) / bpc : 0.0;
            } else {
                u = (timelineSecAt(offset) - L.anchorSec) *
                    std::max(0.0, static_cast<double>(lp.rateHz));
            }
        } else {
            u = L.freePhase; // advanced by advanceTo()
        }
        const double phase = fracPart(u);
        L.lastWrappedPhase = phase;
        if (lp.shape == LfoShape::SampleHold) {
            const auto cycle = static_cast<std::int64_t>(std::floor(u));
            if (cycle != L.lastCycle) {
                L.lastCycle = cycle;
                if (playing) {
                    L.sah = bipolarFromBits(
                        splitmix64(L.seed + static_cast<std::uint64_t>(cycle) * kGolden));
                } else {
                    L.rng = splitmix64(L.rng);
                    L.sah = bipolarFromBits(L.rng);
                }
            }
        }
        return lfoShapeValue(lp.shape, phase, L.sah);
    }

    float envAt(int offset) const noexcept {
        if (envSnapCount < 1 || envSnap.empty())
            return envOut;
        const int idx = std::clamp(tick > 0 ? offset / tick : 0, 0, envSnapCount - 1);
        return envSnap[static_cast<size_t>(idx)];
    }

    double onePoleCoeff(float timeMs) const noexcept {
        const double tau = std::max(0.01, static_cast<double>(timeMs)) * 0.001;
        return 1.0 - std::exp(-1.0 / (tau * fs));
    }

    void beginBlock(const TransportInfo& t, std::span<const NoteEvent> ns, const float* mono,
                    int numSamples) noexcept {
        // 1. Finish the previous block: trailing notes + free-phase tail past the last evaluate.
        if (haveBlock)
            advanceTo(blockLen);

        // 2. Transport edges.
        const bool nowPlaying = t.valid && t.playing;
        if (nowPlaying && !wasPlaying) {
            ++playCount; // "reseed on transport start"
            for (int i = 0; i < 2; ++i) {
                reseedLfo(i);
                lfo[i].lastCycle = kCycleSentinel;
                lfo[i].anchorPpq = 0.0;
                lfo[i].anchorSec = 0.0;
            }
        } else if (!nowPlaying && wasPlaying) {
            for (int i = 0; i < 2; ++i) { // continue free-running from the last played phase
                lfo[i].freePhase = lfo[i].lastWrappedPhase;
                lfo[i].lastCycle = 0; // floor(freePhase in [0,1)) — keep the held S&H value
            }
        }
        wasPlaying = nowPlaying;

        // 3. Adopt the new block context. Sanitize what the host hands us: bpm AND ppq (a
        // broken host's NaN ppq would otherwise poison the timeline phase math and anchors).
        transport = t;
        if (!std::isfinite(transport.ppqPosition))
            transport.ppqPosition = 0.0;
        playing = nowPlaying;
        bpm = (t.valid && t.bpm > 1.0 && std::isfinite(t.bpm)) ? t.bpm : kDefaultBpm;
        beatsPerSample = bpm * invFs / 60.0;
        numNotes = static_cast<int>(std::min(ns.size(), static_cast<size_t>(kMaxBlockNotes)));
        for (int k = 0; k < numNotes; ++k)
            notes[static_cast<size_t>(k)] = ns[static_cast<size_t>(k)];
        noteCursor = 0;
        evalCursor = 0;
        blockLen = std::max(0, numSamples);
        haveBlock = true;

        // 4. Envelope follower: eat the whole block now, snapshot at tick boundaries.
        // Guard sensitivity against non-finite parameter feeds (fallback 0 dB) so a broken
        // host can never latch envState at NaN.
        const float sensDb =
            std::isfinite(params.env.sensitivityDb) ? params.env.sensitivityDb : 0.0f;
        const double gain = std::pow(10.0, static_cast<double>(sensDb) / 20.0);
        const double aAtt = onePoleCoeff(params.env.attackMs);
        const double aRel = onePoleCoeff(params.env.releaseMs);
        const auto cap = static_cast<int>(envSnap.size());
        int count = 0;
        if (cap > 0)
            envSnap[0] = clamp01(envState);
        count = 1;
        for (int s = 0; s < blockLen; ++s) {
            const double x =
                mono != nullptr ? std::abs(static_cast<double>(mono[s])) * gain : 0.0;
            envState += (x > envState ? aAtt : aRel) * (x - envState);
            if (tick > 0 && (s + 1) % tick == 0 && count < cap)
                envSnap[static_cast<size_t>(count++)] = clamp01(envState);
        }
        envSnapCount = std::min(count, std::max(1, cap));
        envOut = clamp01(envState);
    }

    ModValues evaluate(int offset) noexcept {
        advanceTo(offset);
        const float v0 = lfoValueAt(0, offset);
        const float v1 = lfoValueAt(1, offset);
        const float env = envAt(offset);
        ModValues out;
        out.scanOffset =
            v0 * params.lfo1.toScan + v1 * params.lfo2.toScan + env * params.env.toScan;
        out.cutoffSemis = 48.0f * (v0 * params.lfo1.toCutoff + v1 * params.lfo2.toCutoff +
                                   env * params.env.toCutoff) +
                          params.keytrack * static_cast<float>(lastNote - 69);
        return out;
    }

    void reset() noexcept {
        envState = 0.0;
        envOut = 0.0f;
        std::fill(envSnap.begin(), envSnap.end(), 0.0f);
        envSnapCount = 1;
        heldCount = 0;
        lastNote = 69;
        wasPlaying = false;
        playCount = 0;
        transport = {};
        playing = false;
        bpm = kDefaultBpm;
        beatsPerSample = bpm * invFs / 60.0;
        numNotes = 0;
        noteCursor = 0;
        blockLen = 0;
        evalCursor = 0;
        haveBlock = false;
        for (int i = 0; i < 2; ++i) {
            lfo[i] = LfoState{};
            reseedLfo(i);
        }
    }
};

ModulationEngine::ModulationEngine() : impl_(std::make_unique<Impl>()) {
    prepare(48000.0, 64); // safe defaults; the engine re-prepares with real values
}

ModulationEngine::~ModulationEngine() = default;

void ModulationEngine::prepare(double sampleRate, int controlInterval) {
    impl_->fs = sampleRate > 1.0 ? sampleRate : 1.0;
    impl_->invFs = 1.0 / impl_->fs;
    impl_->tick = std::max(1, controlInterval);
    impl_->envSnap.assign(static_cast<size_t>(kMaxSnapshotSamples / impl_->tick) + 2, 0.0f);
    impl_->reset();
}

void ModulationEngine::reset() noexcept {
    impl_->reset();
}

void ModulationEngine::setParams(const Parameters& params) noexcept {
    impl_->params = params;
}

void ModulationEngine::beginBlock(const TransportInfo& transport,
                                  std::span<const NoteEvent> notes, const float* monoInput,
                                  int numSamples) noexcept {
    impl_->beginBlock(transport, notes, monoInput, numSamples);
}

ModValues ModulationEngine::evaluate(int subBlockOffset) noexcept {
    return impl_->evaluate(subBlockOffset);
}

float ModulationEngine::envValue() const noexcept {
    return impl_->envOut;
}

} // namespace ftc
