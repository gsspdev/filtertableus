// PHASE 0 STUB loader: immediately reports "not implemented" so the GUI's load/drop/toast
// paths are exercisable. The wavetable workstream deletes this file and provides
// LoaderServiceImpl.cpp (+ WavImporter, SampleConverter, FactoryGenerators) behind the frozen
// ftus/LoaderService.h seam.
#include "ftus/LoaderService.h"

namespace ftus {

namespace {

class LoaderServiceStub : public LoaderService {
public:
    explicit LoaderServiceStub(ResultCallback cb) : callback_(std::move(cb)) {}

    void requestLoadFile(const juce::File& file) override {
        LoadResult r;
        r.ok = false;
        r.errorMessage = "Wavetable loading is not implemented yet (" + file.getFileName() + ")";
        deliver(r);
    }

    void requestFactoryTable(FactoryTableId id) override {
        LoadResult r;
        r.ok = false;
        r.errorMessage = juce::String("Factory tables are not implemented yet (")
                         + factoryTableDisplayName(id) + ")";
        deliver(r);
    }

    float progress() const override { return 0.0f; }

private:
    void deliver(const LoadResult& r) {
        if (callback_ == nullptr)
            return;
        auto cb = callback_;
        juce::MessageManager::callAsync([cb, r] { cb(r); });
    }

    ResultCallback callback_;
};

} // namespace

std::unique_ptr<LoaderService> createLoaderService(LoaderService::ResultCallback onResult) {
    return std::make_unique<LoaderServiceStub>(std::move(onResult));
}

} // namespace ftus
