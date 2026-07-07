// setWavetable stress test: hammer table adoption (the processor's adoptWavetable path ->
// engine ObjectHandoff publish) at ~50 Hz from a second thread while the main thread renders
// continuously. Validates the ObjectHandoff/graveyard design: no crash, no torn reads, output
// stays finite. The swap thread also runs the message-thread half of the contract
// (collectGarbage on the SAME thread as publish), standing in for the 1 Hz GC timer that
// cannot fire without a running dispatch loop.
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
    std::thread swapper([&] {
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            const int idx = i % static_cast<int>(pool.size());
            proc.adoptWavetable(pool[static_cast<size_t>(idx)],
                                itest::userTableInfo("StressTableSwap"));
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
}
