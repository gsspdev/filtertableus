// FilterTableUS shell — wavetable <-> session-state serialization. FROZEN after Phase 0.
// Implemented (fully working) in source/plugin/WavetableCodecImpl.cpp.
//
// Encoding "gzip-f32le-v1": raw float32-LE frame samples -> juce GZIP -> Base64, stored in a
// <WAVETABLE> ValueTree. Factory-type tables carry NO payload — only their factoryId — and are
// regenerated on load by the caller (StateManager) via generateFactoryTable + analyze; decode
// then returns info with table == nullptr.
//
// Threading: pure functions; safe on any non-audio thread (decode inflates ~2 MB worst case).
#pragma once
#include <optional>

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "ftc/WavetableData.h"

namespace ftus {

struct TableSourceInfo {
    enum class Type { Factory, UserFile, Converted };
    Type type = Type::Factory;
    juce::String factoryId;    // FactoryTables id string when type == Factory
    juce::String path;         // source file path when UserFile/Converted (informational)
    juce::String displayName;
};

juce::ValueTree encodeWavetable(const ftc::WavetableData& table, const TableSourceInfo& info);

struct DecodedTable {
    ftc::WavetablePtr table;   // nullptr when info.type == Factory (regenerate via factoryId)
    TableSourceInfo info;
};

/// Returns nullopt if the tree isn't a valid <WAVETABLE> node or the payload is corrupt.
std::optional<DecodedTable> decodeWavetable(const juce::ValueTree& tree);

} // namespace ftus
