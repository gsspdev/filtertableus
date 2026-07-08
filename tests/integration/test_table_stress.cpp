// setWavetable stress test: hammer table publication (engine.setWavetable -> ObjectHandoff)
// at ~50 Hz from a second thread while the main thread renders continuously. Validates the
// ObjectHandoff/graveyard design: no crash, no torn reads, output stays finite. The swap
// thread runs BOTH halves of the engine's non-audio contract on one thread (publish +
// collectGarbage serialize on the message thread in production; here that role is played by
// the swapper, since no dispatch loop runs during the render).
//
// Wave-3.1: FtusAudioProcessor::adoptWavetable now MARSHALS off-message-thread calls to the
// message thread (hosts restore state from workers), so the swapper drives the engine seam
// directly to keep stressing the handoff; the marshalled processor path is verified
// separately at the end (callAsync delivery once the loop pumps).
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.h"

using itest::JuceEnv;

TEST_CASE_METHOD(JuceEnv,
                 "setWavetable stress: ~50 Hz adoption from a second thread during render",
                 "[integration][stress]") {
    constexpr double kFs = 48000.0;
    constexpr int kBlock = 512;

    ftus::FtusAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    itest::setParamReal(proc, ftus::ids::mix, 1.0f);
    itest::setParamReal(proc, ftus::ids::cutoff, 2000.0f);
    itest::setParamReal(proc, ftus::ids::scan, 0.25f);
    proc.prepareToPlay(kFs, kBlock);

    // Pre-analyze a pool of distinct tables on this thread (analyze allocates; render must not).
    std::vector<ftc::WavetablePtr> pool;
    for (int i = 0; i < 6; ++i)
        pool.push_back(itest::makeFilterTable("StressTable" + std::to_string(i),
                                              {6 + 4 * i, 10 + 6 * i, 30 + 3 * i}));
    proc.adoptWavetable(pool[0], itest::userTableInfo("StressTable0"));

    std::atomic<bool> stop{false};
    std::atomic<int> swaps{0};

    // Swap thread = the "message thread" role: publish + collectGarbage on one thread.
    // Drives engine().setWavetable directly — the processor's adoptWavetable would (correctly)
    // defer off-thread calls to the never-pumped message queue, which stresses nothing.
    std::thread swapper([&] {
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            const int idx = i % static_cast<int>(pool.size());
            proc.engine().setWavetable(pool[static_cast<size_t>(idx)]);
            swaps.fetch_add(1, std::memory_order_relaxed);
            if (++i % 5 == 0)
                proc.engine().collectGarbage();
            std::this_thread::sleep_for(std::chrono::milliseconds(20)); // ~50 Hz
        }
        proc.engine().collectGarbage();
    });

    // Main thread = the audio role: render as fast as possible for a fixed wall-clock window.
    juce::AudioBuffer<float> block(2, kBlock);
    juce::MidiBuffer midi;
    bool everythingFinite = true;
    float maxAbs = 0.0f;
    long long blocksRendered = 0;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    std::uint32_t seed = 0x5EEDu;
    while (std::chrono::steady_clock::now() < deadline) {
        itest::fillNoise(block, ++seed);
        midi.clear();
        proc.processBlock(block, midi);
        for (int ch = 0; ch < 2 && everythingFinite; ++ch) {
            const float* d = block.getReadPointer(ch);
            for (int n = 0; n < kBlock; ++n) {
                if (!std::isfinite(d[n])) {
                    everythingFinite = false;
                    break;
                }
                maxAbs = std::max(maxAbs, std::abs(d[n]));
            }
        }
        ++blocksRendered;
    }

    stop.store(true, std::memory_order_relaxed);
    swapper.join();

    INFO("blocks rendered: " << blocksRendered << ", table swaps: " << swaps.load()
                             << ", max |sample|: " << maxAbs);
    REQUIRE(blocksRendered > 50);
    REQUIRE(swaps.load() >= 30); // ~125 expected at 50 Hz over 2.5 s; generous floor for loaded CI
    REQUIRE(everythingFinite);
    REQUIRE(maxAbs < 100.0f); // no runaway feedback/garbage even mid-crossfade

    // Engine remains fully functional after the storm.
    itest::fillNoise(block, 0xF00Du);
    midi.clear();
    proc.processBlock(block, midi);
    REQUIRE(itest::allFinite(block));
    REQUIRE(proc.engine().currentTableForUi() != nullptr);

    // Wave-3.1 marshal check: adoptWavetable from a NON-message thread must defer the whole
    // adoption (handoff publish + info + broadcast) to the message thread and deliver it once
    // the loop pumps — never publish on the calling thread.
    const auto marshalled = itest::makeFilterTable("MarshalledTable", {5, 25});
    std::thread offThread([&] {
        proc.adoptWavetable(marshalled, itest::userTableInfo("MarshalledTable"));
    });
    offThread.join();
    // Queued, not yet applied (this thread IS the message thread; nothing pumped yet).
    REQUIRE(proc.engine().currentTableForUi() != marshalled);
    itest::pumpMessageLoop(200);
    REQUIRE(proc.engine().currentTableForUi() == marshalled);
    REQUIRE(proc.currentTableInfo().displayName == juce::String("MarshalledTable"));
}
