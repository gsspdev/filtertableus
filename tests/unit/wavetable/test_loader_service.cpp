// LoaderServiceImpl acceptance tests (brief B, acceptance 7).
// Headless message-loop pump: the test thread owns the MessageManager (JuceEnv) and pumps
// it with runDispatchLoopUntil while the loader works on its background thread.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <optional>

#include "WtTestHelpers.h"
#include "ftc/WavetableData.h"
#include "ftus/LoaderService.h"
#include "wavetable/FrameOps.h"

using namespace wt_test;

namespace {

constexpr int kFrame = ftus::wtio::kFrameLength;

struct ResultCatcher {
    std::optional<ftus::LoadResult> result;
    bool deliveredOnMessageThread = false;

    ftus::LoaderService::ResultCallback callback() {
        return [this](const ftus::LoadResult& r) {
            deliveredOnMessageThread =
                juce::MessageManager::getInstance()->isThisTheMessageThread();
            result = r;
        };
    }

    /// Pump the message loop until a result lands (or timeout). Returns success.
    bool pumpUntilDelivered(int timeoutMs = 15000) {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
        while (!result.has_value() &&
               juce::Time::getMillisecondCounterHiRes() < deadline) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }
        return result.has_value();
    }
};

} // namespace

TEST_CASE_METHOD(JuceEnv, "wt: loader delivers a file load asynchronously on the message thread",
                 "[wavetable][loader]") {
    TempDir tmp;
    std::vector<float> samples(static_cast<size_t>(2) * kFrame);
    for (int f = 0; f < 2; ++f)
        for (int n = 0; n < kFrame; ++n)
            samples[static_cast<size_t>(f) * kFrame + static_cast<size_t>(n)] =
                0.8f * std::sin(2.0f * 3.14159265f * static_cast<float>(f + 1) *
                                static_cast<float>(n) / static_cast<float>(kFrame));
    const auto file = writeWav(tmp.dir(), "twoframes.wav", {samples}, 48000.0);

    ResultCatcher catcher;
    auto loader = ftus::createLoaderService(catcher.callback());
    REQUIRE(loader != nullptr);

    loader->requestLoadFile(file);
    // Delivery must be asynchronous: nothing can land before we pump the message loop.
    REQUIRE_FALSE(catcher.result.has_value());

    REQUIRE(catcher.pumpUntilDelivered());
    REQUIRE(catcher.deliveredOnMessageThread);

    const auto& r = *catcher.result;
    REQUIRE(r.ok);
    REQUIRE(r.errorMessage.isEmpty());
    REQUIRE(r.table != nullptr);
    REQUIRE(r.table->numFrames() == 2);
    REQUIRE(r.info.type == ftus::TableSourceInfo::Type::UserFile);
    REQUIRE(r.info.displayName == "twoframes");
    REQUIRE(r.info.path == file.getFullPathName());

    // Progress must have reached 1.0 by delivery time.
    REQUIRE(loader->progress() == 1.0f);
}

TEST_CASE_METHOD(JuceEnv, "wt: loader builds factory tables off-thread",
                 "[wavetable][loader]") {
    ResultCatcher catcher;
    auto loader = ftus::createLoaderService(catcher.callback());

    loader->requestFactoryTable(ftus::FactoryTableId::HarmonicLadder);
    REQUIRE(catcher.pumpUntilDelivered());
    REQUIRE(catcher.deliveredOnMessageThread);

    const auto& r = *catcher.result;
    REQUIRE(r.ok);
    REQUIRE(r.table != nullptr);
    REQUIRE(r.table->numFrames() == 128);
    REQUIRE(r.info.type == ftus::TableSourceInfo::Type::Factory);
    REQUIRE(r.info.factoryId == juce::String("harmonicLadder"));
    REQUIRE(r.info.displayName == juce::String("Harmonic Ladder"));
    REQUIRE(loader->progress() == 1.0f);

    // Second request of the same table (memoized) must still deliver.
    catcher.result.reset();
    loader->requestFactoryTable(ftus::FactoryTableId::HarmonicLadder);
    REQUIRE(catcher.pumpUntilDelivered());
    REQUIRE(catcher.result->ok);
    REQUIRE(catcher.result->table != nullptr);
}

TEST_CASE_METHOD(JuceEnv, "wt: loader reports errors as clean results",
                 "[wavetable][loader]") {
    TempDir tmp;
    const auto garbage = tmp.dir().getChildFile("garbage.wav");
    REQUIRE(garbage.replaceWithText("not audio at all"));

    ResultCatcher catcher;
    auto loader = ftus::createLoaderService(catcher.callback());

    loader->requestLoadFile(garbage);
    REQUIRE(catcher.pumpUntilDelivered());
    REQUIRE(catcher.deliveredOnMessageThread);

    const auto& r = *catcher.result;
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.errorMessage.isNotEmpty());
    REQUIRE(r.table == nullptr);

    // The service survives the error and can still load afterwards.
    catcher.result.reset();
    loader->requestFactoryTable(ftus::FactoryTableId::SubBloom);
    REQUIRE(catcher.pumpUntilDelivered());
    REQUIRE(catcher.result->ok);
    REQUIRE(catcher.result->table != nullptr);
}

TEST_CASE_METHOD(JuceEnv, "wt: loader destruction with a queued job does not crash",
                 "[wavetable][loader]") {
    TempDir tmp;
    const auto missing = tmp.dir().getChildFile("never-there.wav");

    // Shared state: a pending callAsync may fire after the loader (and this scope's
    // locals) are gone, so the callback must own everything it touches.
    auto delivered = std::make_shared<std::atomic<int>>(0);
    {
        auto loader = ftus::createLoaderService(
            [delivered](const ftus::LoadResult&) { delivered->fetch_add(1); });
        loader->requestLoadFile(missing);
        // Destroy immediately — worker may or may not have started the job.
    }
    // Drain whatever was queued to the message thread; must not crash.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    SUCCEED("no crash; deliveries seen: " << delivered->load());
}
