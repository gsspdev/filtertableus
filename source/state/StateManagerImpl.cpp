// FilterTableUS shell — StateManagerImpl: full session state (XML with embedded wavetable),
// factory-table regeneration, .ftpreset preset management, and dirty tracking.
// Replaces the Phase 0 StateManagerStub behind the frozen ftus/StateManager.h seam.
//
// Session schema (stateVersion 1, serialized as UTF-8 XML text):
//   <FilterTableUS stateVersion="1" pluginVersion="...">
//     <PARAMS ...>            APVTS state (parameter values + the "guiScale" property)
//     <WAVETABLE .../>        via ftus::WavetableCodec ("gzip-f32le-v1"; factory = id only)
//     <GUI scale="..."/>      editor scale (mirrors PARAMS.guiScale for the GUI workstream)
//     <PRESET name dirty/>    preset bookkeeping
//   </FilterTableUS>
// Presets use the same schema minus GUI/PRESET with root <FTUSPreset name="...">.
//
// Threading: getState/setState may run off the message thread — everything here sticks to
// APVTS::copyState/replaceState (thread-safe) and post-based change broadcasts; no GUI calls.
// Preset operations are message-thread-only per the frozen header.
#include "ftus/StateManager.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "FtusPresets.h"
#include "ftc/WavetableData.h"
#include "ftus/FactoryTables.h"
#include "ftus/WavetableCodec.h"
#include "plugin/PluginProcessor.h"

namespace ftus {

namespace {

constexpr int kStateVersion = 1;
constexpr const char* kPluginVersion = "0.1.0";
constexpr const char* kSessionRootType = "FilterTableUS";
constexpr const char* kPresetRootType = "FTUSPreset";
constexpr const char* kWavetableType = "WAVETABLE";
constexpr const char* kGuiType = "GUI";
constexpr const char* kPresetType = "PRESET";
/// GUI scale rides on the APVTS tree so the editor can read/write it via the processor state.
constexpr const char* kGuiScaleProperty = "guiScale";
constexpr const char* kPresetExtension = ".ftpreset";

/// User preset folder. FTUS_PRESET_DIR overrides for tests; queried lazily on every use.
juce::File userPresetDirectory() {
    if (const char* env = std::getenv("FTUS_PRESET_DIR"); env != nullptr && env[0] != '\0')
        return juce::File(juce::String::fromUTF8(env));
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support/FilterTableUS/Presets");
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("FilterTableUS/Presets");
#endif
}

juce::ValueTree treeFromXmlText(const juce::String& text) {
    if (auto xml = juce::parseXML(text))
        return juce::ValueTree::fromXml(*xml);
    return {};
}

juce::String presetNameFromTree(const juce::ValueTree& root) {
    return root.isValid() ? root.getProperty("name", juce::String()).toString() : juce::String();
}

struct PresetEntry {
    juce::String name;
    bool factory = false;
    int binaryIndex = -1; // index into FtusPresets::namedResourceList when factory
    juce::File file;      // user preset file otherwise
};

juce::ValueTree loadPresetTree(const PresetEntry& e) {
    if (e.factory) {
        int size = 0;
        const char* data = FtusPresets::getNamedResource(
            FtusPresets::namedResourceList[e.binaryIndex], size);
        if (data == nullptr || size <= 0)
            return {};
        return treeFromXmlText(juce::String::fromUTF8(data, size));
    }
    return treeFromXmlText(e.file.loadFileAsString());
}

} // namespace

class StateManagerImpl : public StateManager,
                         private juce::AudioProcessorValueTreeState::Listener {
public:
    explicit StateManagerImpl(FtusAudioProcessor& processor) : processor_(processor) {
        for (auto* param : processor_.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                paramIds_.add(withId->paramID);
                processor_.state().addParameterListener(withId->paramID, this);
            }
        tableSnapshot_ = processor_.engine().currentTableForUi();
    }

    ~StateManagerImpl() override {
        for (const auto& id : paramIds_)
            processor_.state().removeParameterListener(id, this);
    }

    // --- session state ---------------------------------------------------------------------

    void getState(juce::MemoryBlock& dest) override {
        const auto tree = buildStateTree(kSessionRootType, currentPresetName());
        const auto text = tree.toXmlString();
        juce::MemoryOutputStream out(dest, false);
        out.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
    }

    void setState(const void* data, int sizeInBytes) override {
        if (data == nullptr || sizeInBytes <= 0)
            return;
        auto root =
            treeFromXmlText(juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes));
        if (!root.isValid()) // Phase 0 stub sessions were binary ValueTree streams
            root = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
        if (!root.isValid() || !root.hasType(kSessionRootType))
            return;
        applyStateTree(root, /*isSession=*/true);
    }

    // --- presets (message thread only) -------------------------------------------------------

    juce::StringArray listPresets() override {
        juce::StringArray names;
        for (const auto& e : scanPresets())
            names.add(e.name);
        return names;
    }

    bool loadPreset(const juce::String& name) override {
        for (const auto& e : scanPresets()) {
            if (e.name != name)
                continue;
            const auto root = loadPresetTree(e);
            if (!root.isValid() || !root.hasType(kPresetRootType))
                return false;
            if (!applyStateTree(root, /*isSession=*/false))
                return false;
            presetName_ = name;
            paramDirty_.store(false, std::memory_order_relaxed);
            tableSnapshot_ = processor_.engine().currentTableForUi();
            processor_.sendChangeMessage();
            return true;
        }
        return false;
    }

    bool saveUserPreset(const juce::String& name) override {
        const auto trimmed = name.trim();
        if (trimmed.isEmpty())
            return false;
        for (const auto& e : scanPresets()) // factory presets are read-only
            if (e.factory && e.name == trimmed)
                return false;
        auto dir = userPresetDirectory();
        if (!dir.isDirectory() && !dir.createDirectory().wasOk())
            return false;
        const auto file =
            dir.getChildFile(juce::File::createLegalFileName(trimmed) + kPresetExtension);
        const auto tree = buildStateTree(kPresetRootType, trimmed);
        if (!file.replaceWithText(tree.toXmlString()))
            return false;
        presetName_ = trimmed;
        paramDirty_.store(false, std::memory_order_relaxed);
        tableSnapshot_ = processor_.engine().currentTableForUi();
        processor_.sendChangeMessage();
        return true;
    }

    bool nextPreset() override { return stepPreset(1); }
    bool prevPreset() override { return stepPreset(-1); }

    juce::String currentPresetName() override { return presetName_; }

    bool isDirty() override {
        return paramDirty_.load(std::memory_order_relaxed) ||
               processor_.engine().currentTableForUi() != tableSnapshot_;
    }

private:
    // --- tree building / application ---------------------------------------------------------

    /// Full state tree. Session roots get GUI + PRESET nodes; preset roots get a name property.
    juce::ValueTree buildStateTree(const juce::String& rootType, const juce::String& presetName) {
        const bool isSession = rootType == kSessionRootType;
        juce::ValueTree root{juce::Identifier(rootType)};
        root.setProperty("stateVersion", kStateVersion, nullptr);
        root.setProperty("pluginVersion", kPluginVersion, nullptr);
        if (!isSession)
            root.setProperty("name", presetName, nullptr);

        auto params = processor_.state().copyState();
        root.appendChild(params, nullptr);

        const auto info = processor_.currentTableInfo();
        if (const auto table = processor_.engine().currentTableForUi()) {
            root.appendChild(encodeWavetable(*table, info), nullptr);
        } else if (info.type == TableSourceInfo::Type::Factory && info.factoryId.isNotEmpty()) {
            // No table adopted yet (or engine mirror unavailable): keep the factory reference.
            juce::ValueTree wt(kWavetableType);
            wt.setProperty("type", "factory", nullptr);
            wt.setProperty("factoryId", info.factoryId, nullptr);
            wt.setProperty("name", info.displayName, nullptr);
            root.appendChild(wt, nullptr);
        }

        if (isSession) {
            juce::ValueTree gui(kGuiType);
            gui.setProperty("scale", params.getProperty(kGuiScaleProperty, 1.0), nullptr);
            root.appendChild(gui, nullptr);

            juce::ValueTree preset(kPresetType);
            preset.setProperty("name", presetName, nullptr);
            preset.setProperty("dirty", isDirty() ? 1 : 0, nullptr);
            root.appendChild(preset, nullptr);
        }
        return root;
    }

    /// Applies PARAMS + WAVETABLE (+ PRESET bookkeeping for sessions). Returns true when the
    /// tree contained an applicable PARAMS child.
    bool applyStateTree(const juce::ValueTree& root, bool isSession) {
        auto& apvts = processor_.state();
        bool appliedParams = false;

        if (auto params = root.getChildWithName(apvts.state.getType()); params.isValid()) {
            auto copy = params.createCopy();
            if (isSession) {
                if (const auto gui = root.getChildWithName(kGuiType);
                    gui.isValid() && gui.hasProperty("scale"))
                    copy.setProperty(kGuiScaleProperty, gui.getProperty("scale"), nullptr);
            } else {
                // Presets never move the editor scale: carry the current one across.
                const auto current = apvts.copyState();
                if (current.hasProperty(kGuiScaleProperty))
                    copy.setProperty(kGuiScaleProperty, current.getProperty(kGuiScaleProperty),
                                     nullptr);
            }
            applying_.store(true, std::memory_order_relaxed);
            apvts.replaceState(copy);
            applying_.store(false, std::memory_order_relaxed);
            appliedParams = true;
        }

        if (const auto wt = root.getChildWithName(kWavetableType); wt.isValid()) {
            if (auto decoded = decodeWavetable(wt)) {
                if (decoded->info.type == TableSourceInfo::Type::Factory)
                    regenerateFactoryTable(decoded->info);
                else if (decoded->table != nullptr)
                    processor_.adoptWavetable(decoded->table, decoded->info);
            }
        }

        if (isSession) {
            const auto preset = root.getChildWithName(kPresetType);
            presetName_ = preset.isValid()
                              ? preset.getProperty("name", "Init").toString()
                              : juce::String("Init");
            paramDirty_.store(preset.isValid() &&
                                  static_cast<int>(preset.getProperty("dirty", 0)) != 0,
                              std::memory_order_relaxed);
            tableSnapshot_ = processor_.engine().currentTableForUi();
        }
        return appliedParams;
    }

    /// generateFactoryTable -> WavetableData::analyze -> processor adoption.
    bool regenerateFactoryTable(const TableSourceInfo& info) {
        const auto id = factoryTableIdFromString(info.factoryId);
        if (id == FactoryTableId::NumTables)
            return false;
        const auto raw = generateFactoryTable(id);
        const auto expectedSamples = static_cast<size_t>(raw.numFrames) *
                                     static_cast<size_t>(ftc::WavetableData::kFrameLength);
        if (raw.numFrames < 1 || raw.numFrames > ftc::WavetableData::kMaxFrames ||
            raw.samples.size() != expectedSamples)
            return false;
        const auto displayName = info.displayName.isNotEmpty()
                                     ? info.displayName
                                     : juce::String(factoryTableDisplayName(id));
        auto table =
            ftc::WavetableData::analyze(raw.samples, raw.numFrames, displayName.toStdString());
        if (table == nullptr)
            return false;
        TableSourceInfo adopted = info;
        adopted.type = TableSourceInfo::Type::Factory;
        adopted.factoryId = factoryTableIdString(id);
        adopted.displayName = displayName;
        processor_.adoptWavetable(std::move(table), adopted);
        return true;
    }

    // --- preset scanning ---------------------------------------------------------------------

    std::vector<PresetEntry> scanPresets() const {
        std::vector<PresetEntry> list;
        for (int i = 0; i < FtusPresets::namedResourceListSize; ++i) {
            const juce::String original(FtusPresets::originalFilenames[i]);
            if (!original.endsWithIgnoreCase(kPresetExtension))
                continue;
            int size = 0;
            const char* data =
                FtusPresets::getNamedResource(FtusPresets::namedResourceList[i], size);
            if (data == nullptr || size <= 0)
                continue;
            auto name = presetNameFromTree(treeFromXmlText(juce::String::fromUTF8(data, size)));
            if (name.isEmpty())
                name = original.upToLastOccurrenceOf(".", false, false);
            list.push_back({name, true, i, {}});
        }

        std::vector<PresetEntry> user;
        for (const auto& file : userPresetDirectory().findChildFiles(
                 juce::File::findFiles, false, juce::String("*") + kPresetExtension)) {
            auto name = presetNameFromTree(treeFromXmlText(file.loadFileAsString()));
            if (name.isEmpty())
                name = file.getFileNameWithoutExtension();
            user.push_back({name, false, -1, file});
        }
        std::sort(user.begin(), user.end(), [](const PresetEntry& a, const PresetEntry& b) {
            return a.name.compareIgnoreCase(b.name) < 0;
        });
        for (auto& u : user) {
            const bool duplicate =
                std::any_of(list.begin(), list.end(),
                            [&u](const PresetEntry& e) { return e.name == u.name; });
            if (!duplicate)
                list.push_back(std::move(u));
        }
        return list;
    }

    bool stepPreset(int delta) {
        const auto names = listPresets();
        if (names.isEmpty())
            return false;
        const int idx = names.indexOf(presetName_);
        const int n = names.size();
        const int target = idx < 0 ? (delta > 0 ? 0 : n - 1) : ((idx + delta) % n + n) % n;
        return loadPreset(names[target]);
    }

    // --- dirty tracking ----------------------------------------------------------------------

    void parameterChanged(const juce::String&, float) override {
        // May fire on the audio thread (host automation): atomics only, nothing else.
        if (!applying_.load(std::memory_order_relaxed))
            paramDirty_.store(true, std::memory_order_relaxed);
    }

    FtusAudioProcessor& processor_;
    juce::StringArray paramIds_;
    juce::String presetName_{"Init"};
    std::atomic<bool> paramDirty_{false};
    std::atomic<bool> applying_{false};
    ftc::WavetablePtr tableSnapshot_;
};

std::unique_ptr<StateManager> createStateManager(FtusAudioProcessor& processor) {
    return std::make_unique<StateManagerImpl>(processor);
}

} // namespace ftus
