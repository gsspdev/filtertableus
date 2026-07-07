// FilterTableUS wavetable I/O — async loader behind the frozen ftus/LoaderService.h seam.
// Owned by the wavetable workstream (replaces the Phase 0 LoaderServiceStub).
//
// One background juce::Thread with a mutex-guarded job deque. requestLoadFile /
// requestFactoryTable enqueue from the message thread only; the worker decodes/converts,
// runs ftc::WavetableData::analyze, and delivers the LoadResult on the message thread via
// MessageManager::callAsync. progress() is an atomic float, 0..1. Errors come back as clean
// LoadResult{ok=false, errorMessage}; malformed input never crashes (worker jobs are
// exception-fenced). Factory tables are generated on the worker thread and memoized as
// analyzed WavetablePtrs (worker-only access, jobs are serial).
#include "ftus/LoaderService.h"

#include <array>
#include <atomic>
#include <deque>
#include <exception>

#include <juce_events/juce_events.h>

#include "wavetable/WavImporter.h"

namespace ftus {

namespace {

class LoaderServiceImpl : public LoaderService, private juce::Thread {
public:
    explicit LoaderServiceImpl(ResultCallback cb)
        : juce::Thread("FTUS Wavetable Loader"), callback_(std::move(cb)) {
        startThread();
    }

    ~LoaderServiceImpl() override {
        signalThreadShouldExit();
        notify();
        stopThread(5000);
    }

    void requestLoadFile(const juce::File& file) override {
        JUCE_ASSERT_MESSAGE_THREAD
        {
            const juce::ScopedLock sl(queueLock_);
            jobs_.push_back(Job{Job::Type::file, file, FactoryTableId::AnalogMorph});
        }
        progress_.store(0.0f, std::memory_order_relaxed);
        notify();
    }

    void requestFactoryTable(FactoryTableId id) override {
        JUCE_ASSERT_MESSAGE_THREAD
        {
            const juce::ScopedLock sl(queueLock_);
            jobs_.push_back(Job{Job::Type::factory, juce::File(), id});
        }
        progress_.store(0.0f, std::memory_order_relaxed);
        notify();
    }

    float progress() const override { return progress_.load(std::memory_order_relaxed); }

private:
    struct Job {
        enum class Type { file, factory };
        Type type = Type::file;
        juce::File file;
        FactoryTableId id = FactoryTableId::AnalogMorph;
    };

    void run() override {
        while (!threadShouldExit()) {
            Job job;
            bool hasJob = false;
            {
                const juce::ScopedLock sl(queueLock_);
                if (!jobs_.empty()) {
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                    hasJob = true;
                }
            }
            if (!hasJob) {
                wait(-1); // notify() wakes us; pending notifies are not lost
                continue;
            }

            LoadResult result = runJobSafely(job);
            progress_.store(1.0f, std::memory_order_relaxed);
            deliver(std::move(result));
        }
    }

    LoadResult runJobSafely(const Job& job) noexcept {
        try {
            return job.type == Job::Type::file ? loadFile(job.file) : loadFactory(job.id);
        } catch (const std::exception& e) {
            LoadResult r;
            r.ok = false;
            r.errorMessage = juce::String("Wavetable load failed: ") + e.what();
            return r;
        } catch (...) {
            LoadResult r;
            r.ok = false;
            r.errorMessage = "Wavetable load failed unexpectedly";
            return r;
        }
    }

    LoadResult loadFile(const juce::File& file) {
        LoadResult r;
        progress_.store(0.05f, std::memory_order_relaxed);

        ImportResult imported = WavImporter::importFile(file);
        if (!imported.ok) {
            r.errorMessage = imported.errorMessage;
            return r;
        }
        progress_.store(0.65f, std::memory_order_relaxed);

        const juce::String displayName = file.getFileNameWithoutExtension();
        auto table = ftc::WavetableData::analyze(imported.frames, imported.numFrames,
                                                 displayName.toStdString());
        if (table == nullptr) {
            r.errorMessage = "Couldn't analyze " + file.getFileName();
            return r;
        }
        progress_.store(0.95f, std::memory_order_relaxed);

        r.ok = true;
        r.table = std::move(table);
        r.info.type = imported.sourceType;
        r.info.path = file.getFullPathName();
        r.info.displayName = displayName;
        return r;
    }

    LoadResult loadFactory(FactoryTableId id) {
        LoadResult r;
        const int index = static_cast<int>(id);
        if (index < 0 || index >= kNumFactoryTables) {
            r.errorMessage = "Unknown factory table";
            return r;
        }
        progress_.store(0.1f, std::memory_order_relaxed);

        // Memoized on the worker thread only (jobs are serial — no lock needed).
        if (factoryCache_[static_cast<size_t>(index)] == nullptr) {
            const RawTable raw = generateFactoryTable(id);
            progress_.store(0.6f, std::memory_order_relaxed);
            auto table = ftc::WavetableData::analyze(raw.samples, raw.numFrames, raw.name);
            if (table == nullptr) {
                r.errorMessage = juce::String("Couldn't build factory table ") +
                                 factoryTableDisplayName(id);
                return r;
            }
            factoryCache_[static_cast<size_t>(index)] = std::move(table);
        }
        progress_.store(0.95f, std::memory_order_relaxed);

        r.ok = true;
        r.table = factoryCache_[static_cast<size_t>(index)];
        r.info.type = TableSourceInfo::Type::Factory;
        r.info.factoryId = factoryTableIdString(id);
        r.info.displayName = factoryTableDisplayName(id);
        return r;
    }

    void deliver(LoadResult result) {
        if (callback_ == nullptr)
            return;
        // Copy the callback so the lambda never touches `this` (the service may be
        // destroyed while the message is still queued).
        auto cb = callback_;
        juce::MessageManager::callAsync(
            [cb, r = std::move(result)] { cb(r); });
    }

    ResultCallback callback_;
    juce::CriticalSection queueLock_;
    std::deque<Job> jobs_;
    std::atomic<float> progress_{0.0f};
    std::array<ftc::WavetablePtr, kNumFactoryTables> factoryCache_{};
};

} // namespace

std::unique_ptr<LoaderService> createLoaderService(LoaderService::ResultCallback onResult) {
    return std::make_unique<LoaderServiceImpl>(std::move(onResult));
}

} // namespace ftus
