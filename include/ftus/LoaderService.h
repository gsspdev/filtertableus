// FilterTableUS shell — async wavetable loading interface. FROZEN after Phase 0.
// Implemented by the wavetable workstream (source/wavetable/LoaderServiceImpl.cpp); Phase 0
// ships an error-only stub (source/wavetable/LoaderServiceStub.cpp).
//
// Threading: requestLoadFile/requestFactoryTable are message-thread-only. Work happens on the
// service's own background thread. The result callback ALWAYS fires on the message thread.
// progress() is safe from the message thread (atomic-backed), 0..1.
#pragma once
#include <functional>
#include <memory>

#include <juce_core/juce_core.h>

#include "ftc/WavetableData.h"
#include "ftus/FactoryTables.h"
#include "ftus/WavetableCodec.h"

namespace ftus {

struct LoadResult {
    bool ok = false;
    juce::String errorMessage;
    ftc::WavetablePtr table;   // analyzed, ready for the engine (null on error)
    TableSourceInfo info;
};

class LoaderService {
public:
    using ResultCallback = std::function<void(const LoadResult&)>;

    virtual ~LoaderService() = default;
    virtual void requestLoadFile(const juce::File& file) = 0;
    virtual void requestFactoryTable(FactoryTableId id) = 0;
    virtual float progress() const = 0;
};

/// Factory — implemented in source/wavetable/. The callback fires on the message thread.
std::unique_ptr<LoaderService> createLoaderService(LoaderService::ResultCallback onResult);

} // namespace ftus
