// Wavetable <-> ValueTree codec ("gzip-f32le-v1"). FROZEN after Phase 0.
#include "ftus/WavetableCodec.h"

namespace ftus {

namespace {
constexpr const char* kNodeType = "WAVETABLE";
constexpr const char* kEncoding = "gzip-f32le-v1";

juce::String typeToString(TableSourceInfo::Type t) {
    switch (t) {
        case TableSourceInfo::Type::Factory: return "factory";
        case TableSourceInfo::Type::UserFile: return "user";
        case TableSourceInfo::Type::Converted: return "converted";
    }
    return "user";
}

TableSourceInfo::Type typeFromString(const juce::String& s) {
    if (s == "factory") return TableSourceInfo::Type::Factory;
    if (s == "converted") return TableSourceInfo::Type::Converted;
    return TableSourceInfo::Type::UserFile;
}
} // namespace

juce::ValueTree encodeWavetable(const ftc::WavetableData& table, const TableSourceInfo& info) {
    juce::ValueTree tree(kNodeType);
    tree.setProperty("type", typeToString(info.type), nullptr);
    tree.setProperty("factoryId", info.factoryId, nullptr);
    tree.setProperty("path", info.path, nullptr);
    tree.setProperty("name", info.displayName, nullptr);
    tree.setProperty("frames", table.numFrames(), nullptr);
    tree.setProperty("encoding", kEncoding, nullptr);

    if (info.type != TableSourceInfo::Type::Factory) {
        juce::MemoryOutputStream raw;
        {
            juce::GZIPCompressorOutputStream gz(raw);
            for (int f = 0; f < table.numFrames(); ++f) {
                const auto frame = table.frame(f);
                gz.write(frame.data(), frame.size_bytes()); // f32 native-LE on all targets
            }
            gz.flush();
        }
        tree.setProperty("data", raw.getMemoryBlock().toBase64Encoding(), nullptr);
    }
    return tree;
}

std::optional<DecodedTable> decodeWavetable(const juce::ValueTree& tree) {
    if (!tree.hasType(kNodeType))
        return std::nullopt;

    DecodedTable out;
    out.info.type = typeFromString(tree.getProperty("type").toString());
    out.info.factoryId = tree.getProperty("factoryId").toString();
    out.info.path = tree.getProperty("path").toString();
    out.info.displayName = tree.getProperty("name").toString();

    if (out.info.type == TableSourceInfo::Type::Factory)
        return out; // no payload — caller regenerates from factoryId

    if (tree.getProperty("encoding").toString() != kEncoding)
        return std::nullopt;

    const int numFrames = static_cast<int>(tree.getProperty("frames", 0));
    if (numFrames < 1 || numFrames > ftc::WavetableData::kMaxFrames)
        return std::nullopt;

    juce::MemoryBlock compressed;
    if (!compressed.fromBase64Encoding(tree.getProperty("data").toString()))
        return std::nullopt;

    const size_t expectedFloats =
        static_cast<size_t>(numFrames) * ftc::WavetableData::kFrameLength;
    std::vector<float> samples(expectedFloats);

    juce::MemoryInputStream compressedIn(compressed, false);
    juce::GZIPDecompressorInputStream gz(&compressedIn, false);
    const auto wanted = static_cast<int>(expectedFloats * sizeof(float));
    if (gz.read(samples.data(), wanted) != wanted)
        return std::nullopt;

    out.table = ftc::WavetableData::analyze(
        samples, numFrames, out.info.displayName.toStdString());
    if (out.table == nullptr)
        return std::nullopt;
    return out;
}

} // namespace ftus
