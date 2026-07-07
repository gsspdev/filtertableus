// A1 acceptance tests: real-time safety (no allocation after prepare) + optional benchmark.
//
// The no-alloc proof hooks macOS libmalloc's `malloc_logger`, which fires for every
// malloc/calloc/realloc/free in the process (operator new routes through malloc on Darwin).
// Counting is gated by a thread_local flag so only the test thread's events are counted.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "ftc/Convolver.h"
#include "ftc/EngineConfig.h"
#include "ftc/Kernel.h"
#include "test_convolver_utils.h"

using ftc::ConvolutionSection;
using ftc::Kernel;
using ftc::KernelImage;
using ftc::PartitionedConvolver;

namespace {

PartitionedConvolver::Config defaultConfig() {
    PartitionedConvolver::Config cfg;
    cfg.maxKernelLength = 2048;
    cfg.headLength = ftc::EngineConfig::headLength;
    cfg.partitionLength = ftc::EngineConfig::partitionLength;
    cfg.maxBlockSize = 8192;
    return cfg;
}

} // namespace

#if defined(__APPLE__)

extern "C" {
typedef void(malloc_logger_t)(uint32_t type, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
                              uintptr_t result, uint32_t num_hot_frames_to_skip);
extern malloc_logger_t* malloc_logger;
}

namespace {
std::atomic<std::uint64_t> gAllocEvents{0};
thread_local bool tlsCountAllocs = false;

void countingLogger(uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t) {
    if (tlsCountAllocs)
        gAllocEvents.fetch_add(1, std::memory_order_relaxed);
}

struct AllocationWatch {
    malloc_logger_t* previous;
    AllocationWatch() : previous(malloc_logger) {
        gAllocEvents.store(0, std::memory_order_relaxed);
        tlsCountAllocs = true;
        malloc_logger = &countingLogger;
    }
    ~AllocationWatch() {
        malloc_logger = previous;
        tlsCountAllocs = false;
    }
    std::uint64_t events() const { return gAllocEvents.load(std::memory_order_relaxed); }
};
} // namespace

TEST_CASE("no allocation on any RT call after prepare", "[convolver][rt]") {
    // Positive control: the hook must observe a deliberate heap allocation, otherwise a
    // zero count below would prove nothing.
    {
        // Volatile function pointers force real malloc/free calls (no allocation elision).
        void* (*volatile mallocFn)(size_t) = std::malloc;
        void (*volatile freeFn)(void*) = std::free;
        AllocationWatch watch;
        void* p = mallocFn(1024);
        freeFn(p);
        REQUIRE(watch.events() > 0);
    }

    const auto cfg = defaultConfig();
    const auto kernelA = ftct::randomKernel(2048, 91u, 0.05f);
    const auto kernelB = ftct::randomKernel(1000, 92u, 0.05f);
    const auto x = ftct::randomSignal(8192, 93u);

    PartitionedConvolver conv, conv2;
    conv.prepare(cfg);
    conv2.prepare(cfg);
    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);

    ConvolutionSection section;
    section.prepare(cfg, 2, 128);

    std::vector<float> out(x.size(), 0.0f);
    std::vector<float> ch0 = x, ch1 = x;
    float* chans[2] = {ch0.data(), ch1.data()};

    // Warm-up outside the watch: bind lazy dyld symbols, fault code paths.
    conv.analyze(kernelA, img);
    conv.process(img, x.data(), out.data(), 512);
    section.setKernelImmediate(kernelA);
    section.process(chans, 512);

    std::uint64_t events = 0;
    {
        AllocationWatch watch;

        conv.reset();
        conv.analyze(kernelA, img);
        const int sizes[6] = {1, 17, 64, 441, 3, 128};
        int pos = 0, si = 0;
        while (pos < 4096) {
            int n = sizes[si % 6];
            ++si;
            n = std::min(n, 4096 - pos);
            conv.process(img, x.data() + pos, out.data() + pos, n);
            pos += n;
        }
        conv.analyze(kernelB, img); // re-analyze mid-stream (kernel tick)
        conv.process(img, x.data() + pos, out.data() + pos, 1024);
        conv2.copyStateFrom(conv);
        conv2.process(img, x.data() + pos, out.data() + pos, 1024);
        conv.reset();

        section.setKernelImmediate(kernelA);
        section.process(chans, 1000);
        (void)section.pushKernel(kernelB);
        section.process(chans, 40);          // mid-fade
        (void)section.pushKernel(kernelA);   // rejected while fading
        section.process(chans, 512);         // completes the fade, back to steady state
        (void)section.isFading();
        (void)section.currentLatencySamples();
        section.reset();

        events = watch.events();
    }
    REQUIRE(events == 0);
}

#else

TEST_CASE("no allocation on any RT call after prepare", "[convolver][rt]") {
    SKIP("allocation hook (malloc_logger) is only wired up on macOS");
}

#endif // __APPLE__

TEST_CASE("throughput benchmark, L=2048, block 512", "[.perf][convolver]") {
    const auto cfg = defaultConfig();
    const auto kernel = ftct::randomKernel(2048, 3110u, 0.05f);
    const double fs = 48000.0;
    const int block = 512;
    const int seconds = 10;
    const int total = static_cast<int>(fs) * seconds;

    PartitionedConvolver conv;
    conv.prepare(cfg);
    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);
    conv.analyze(kernel, img);

    std::vector<float> in = ftct::randomSignal(block, 1u), out(static_cast<size_t>(block));
    conv.process(img, in.data(), out.data(), block); // warm-up

    const auto t0 = std::chrono::steady_clock::now();
    for (int done = 0; done < total; done += block)
        conv.process(img, in.data(), out.data(), block);
    const auto t1 = std::chrono::steady_clock::now();

    const double secondsTaken = std::chrono::duration<double>(t1 - t0).count();
    const double audioSeconds = static_cast<double>(total) / fs;
    const double xRealtime = audioSeconds / secondsTaken;
    const double samplesPerSec = static_cast<double>(total) / secondsTaken;
    WARN("PartitionedConvolver mono, L=2048, block=512: "
         << samplesPerSec / 1e6 << " Msamples/s, " << xRealtime << "x realtime @48k ("
         << 100.0 / xRealtime << "% of one core per mono stream)");
    REQUIRE(xRealtime > 1.0);
}
