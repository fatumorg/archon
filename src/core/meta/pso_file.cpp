#include "core/meta/pso_file.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <optional>
#include <string>

namespace archon {

namespace {

constexpr std::uint32_t pseudoEntry = 0x100;
constexpr std::byte fill { 0x70 };
constexpr std::size_t sectionHeader = 8;
constexpr std::size_t psinHeader = 16;
constexpr std::size_t blockAlignment = 16;
constexpr std::uint32_t stringBlock = 0x01;
constexpr std::uint32_t pointerBlock = 0x0C;
constexpr std::uint32_t atStringBlock = 0xC8;

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "pso", 0, std::move(message) });
}

std::uint32_t be32(std::span<const std::byte> data, std::size_t at)
{
    return std::to_integer<std::uint32_t>(data[at]) << 24 | std::to_integer<std::uint32_t>(data[at + 1]) << 16 | std::to_integer<std::uint32_t>(data[at + 2]) << 8 | std::to_integer<std::uint32_t>(data[at + 3]);
}

std::uint16_t be16(std::span<const std::byte> data, std::size_t at)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint32_t>(data[at]) << 8 | std::to_integer<std::uint32_t>(data[at + 1]));
}

void append32(std::vector<std::byte> &out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
}

void append16(std::vector<std::byte> &out, std::uint16_t value)
{
    out.push_back(static_cast<std::byte>(value >> 8));
    out.push_back(static_cast<std::byte>(value & 0xFF));
}

struct Section {
    std::string name;
    std::span<const std::byte> bytes;
};

Result<std::vector<Section>> sections(std::span<const std::byte> file)
{
    std::vector<Section> found;
    std::size_t at = 0;
    while (at < file.size()) {
        if (file.size() - at < sectionHeader)
            return failure("a section header is cut short");
        const std::uint32_t length = be32(file, at + 4);
        if (length < sectionHeader || length > file.size() - at)
            return failure("a section has an invalid length");
        found.push_back(Section { std::string(reinterpret_cast<const char *>(file.data() + at), 4), file.subspan(at, length) });
        at += length;
    }
    return found;
}

const Section *section(const std::vector<Section> &list, std::string_view name)
{
    const auto found = std::ranges::find(list, name, &Section::name);
    return found == list.end() ? nullptr : &*found;
}

struct RawEntry {
    std::uint32_t name;
    std::uint8_t type;
    std::uint8_t subtype;
    std::uint16_t offset;
    std::uint32_t reference;
};

struct RawStruct {
    std::uint32_t name;
    std::uint32_t length;
    std::vector<RawEntry> entries;
};

class SchemaBuilder {
public:
    explicit SchemaBuilder(std::unordered_map<std::uint32_t, RawStruct> raw)
        : raw_(std::move(raw))
    {
    }

    Result<void> build(MetaSchema &schema)
    {
        for (const auto &[hash, type] : raw_) {
            const auto converted = convertStruct(type);
            if (!converted)
                return std::unexpected(converted.error());
            schema.structs.emplace(hash, std::move(*converted));
        }
        return {};
    }

private:
    Result<std::uint32_t> structAlign(std::uint32_t hash, int depth)
    {
        if (const auto found = aligns_.find(hash); found != aligns_.end())
            return found->second;
        const auto type = raw_.find(hash);
        if (type == raw_.end())
            return std::uint32_t(1);
        if (depth > 32)
            return failure("structures contain each other");
        std::uint32_t align = 1;
        std::optional<std::uint32_t> firstOffset;
        for (const RawEntry &entry : type->second.entries) {
            if (entry.name == pseudoEntry)
                continue;
            const auto field = convert(type->second, entry, depth + 1);
            if (!field)
                return std::unexpected(field.error());
            align = std::max(align, field->align);
            firstOffset = std::min(firstOffset.value_or(entry.offset), static_cast<std::uint32_t>(entry.offset));
        }
        if (firstOffset && *firstOffset >= 8)
            align = std::max<std::uint32_t>(align, 8);
        aligns_[hash] = align;
        return align;
    }

    std::uint32_t blockType(const RawEntry &entry) const
    {
        switch (entry.type) {
        case 0x0B:
            return entry.subtype == 3 ? atStringBlock : entry.subtype == 1 || entry.subtype == 2 ? pointerBlock : 0x06;
        case 0x0C:
            return entry.subtype == 0 ? entry.reference : pointerBlock;
        case 0x0E:
        case 0x0F:
            return entry.subtype == 1 ? 0x04 : entry.subtype == 2 ? 0x02 : 0x06;
        default:
            return entry.type;
        }
    }

    Result<MetaField> convert(const RawStruct &owner, const RawEntry &entry, int depth)
    {
        MetaField field;
        field.name = entry.name;
        field.offset = entry.offset;
        field.blockType = blockType(entry);
        const auto simple = [&field](MetaType type, std::uint32_t size, std::uint32_t align) {
            field.type = type;
            field.size = size;
            field.align = align;
            return field;
        };
        const auto element = [&owner](std::uint32_t index) -> std::optional<RawEntry> {
            if (index >= owner.entries.size())
                return std::nullopt;
            return owner.entries[index];
        };
        switch (entry.type) {
        case 0x00:
            return simple(MetaType::Bool, 1, 1);
        case 0x01:
            return simple(MetaType::Int8, 1, 1);
        case 0x02:
            return simple(MetaType::UInt8, 1, 1);
        case 0x03:
            return simple(MetaType::Int16, 2, 2);
        case 0x04:
            return simple(MetaType::UInt16, 2, 2);
        case 0x05:
            return simple(MetaType::Int32, 4, 4);
        case 0x06:
            return simple(entry.subtype == 1 ? MetaType::Color : MetaType::UInt32, 4, 4);
        case 0x07:
            return simple(MetaType::Float, 4, 4);
        case 0x08:
            return simple(MetaType::Vector2, 8, 4);
        case 0x09:
        case 0x14:
            return simple(MetaType::Vector3, 16, 16);
        case 0x0A:
        case 0x15:
            return simple(MetaType::Vector4, 16, 16);
        case 0x1E:
            return simple(MetaType::Half, 2, 2);
        case 0x20:
            return simple(MetaType::Int64, 8, 8);
        case 0x0B:
            switch (entry.subtype) {
            case 0:
                return simple(MetaType::FixedString, entry.reference >> 16, 1);
            case 1:
            case 2:
                return simple(MetaType::CString, 8, 8);
            case 3:
                return simple(MetaType::AtString, 16, 8);
            case 7:
            case 8:
                return simple(MetaType::Hash, 4, 4);
            default:
                break;
            }
            break;
        case 0x0C:
            if (entry.subtype == 0) {
                field.reference = entry.reference;
                const auto type = raw_.find(entry.reference);
                const auto align = structAlign(entry.reference, depth);
                if (!align)
                    return std::unexpected(align.error());
                return simple(MetaType::Struct, type == raw_.end() ? 0 : type->second.length, *align);
            }
            if (entry.subtype == 3)
                return simple(MetaType::StructPointer, 8, 8);
            break;
        case 0x0D: {
            const auto item = element(entry.reference & 0xFFFF);
            if (!item)
                return failure("an array names an item description that does not exist");
            field.element = static_cast<int>(entry.reference & 0xFFFF);
            const std::uint32_t count = entry.reference >> 16;
            if (entry.subtype == 0)
                return simple(MetaType::Array, 16, 8);
            if (entry.subtype == 6) {
                field.count = count;
                return simple(MetaType::PointerArray, 8, 8);
            }
            if (depth > 32)
                return failure("arrays contain each other");
            const auto itemField = convert(owner, *item, depth + 1);
            if (!itemField)
                return std::unexpected(itemField.error());
            field.count = count;
            if (std::uint64_t(count) * itemField->size + 4 > 0xFFFFFFFF)
                return failure("an array is larger than a structure can hold");
            if ((entry.subtype & 0x7F) == 1)
                return simple(MetaType::CountedArray, count * itemField->size + 4, std::max<std::uint32_t>(itemField->align, 4));
            if (entry.subtype == 2 || entry.subtype == 4)
                return simple(MetaType::FixedArray, count * itemField->size, itemField->align);
            break;
        }
        case 0x0E:
        case 0x0F: {
            const std::uint32_t width = entry.subtype == 1 ? 2 : entry.subtype == 2 ? 1 : 4;
            if (entry.subtype > 2)
                break;
            if (entry.type == 0x0E) {
                field.reference = entry.reference;
            } else if (const auto described = element(entry.reference & 0xFFFF); described && described->type == 0x0E) {
                field.reference = described->reference;
            }
            return simple(entry.type == 0x0E ? MetaType::Enum : MetaType::Flags, width, width);
        }
        case 0x10: {
            const auto key = element(entry.reference >> 16);
            const auto value = element(entry.reference & 0xFFFF);
            if (!key || !value || entry.subtype != 1)
                break;
            field.key = static_cast<int>(entry.reference >> 16);
            field.element = static_cast<int>(entry.reference & 0xFFFF);
            for (const auto &[hash, candidate] : raw_) {
                std::vector<RawEntry> members;
                for (const RawEntry &member : candidate.entries) {
                    if (member.name != pseudoEntry)
                        members.push_back(member);
                }
                const auto same = [](const RawEntry &a, const RawEntry &b) { return a.type == b.type && a.subtype == b.subtype && a.reference == b.reference; };
                if (members.size() == 2 && same(members[0], *key) && same(members[1], *value)) {
                    field.reference = hash;
                    break;
                }
            }
            return simple(MetaType::Map, 24, 8);
        }
        default:
            break;
        }
        return failure("a field uses type " + std::to_string(entry.type) + "." + std::to_string(entry.subtype) + ", which ARCHON does not read");
    }

    Result<MetaStruct> convertStruct(const RawStruct &type)
    {
        MetaStruct result;
        result.name = type.name;
        result.size = type.length;
        const auto align = structAlign(type.name, 0);
        if (!align)
            return std::unexpected(align.error());
        result.align = *align;
        std::uint64_t previousEnd = 0;
        for (const RawEntry &entry : type.entries) {
            auto field = convert(type, entry, 0);
            if (!field && entry.name != pseudoEntry)
                return std::unexpected(field.error());
            MetaField converted = field ? *field : MetaField {};
            if (entry.name != pseudoEntry) {
                std::uint64_t offset = entry.offset;
                if (type.length > 0xFFFF) {
                    while (offset + 0x100 < previousEnd && offset + 0x10000 < type.length)
                        offset += 0x10000;
                }
                if (offset + converted.size > type.length)
                    return failure("a field lies outside its structure");
                converted.offset = static_cast<std::uint32_t>(offset);
                previousEnd = std::max(previousEnd, offset + converted.size);
                result.members.push_back(result.entries.size());
            }
            result.entries.push_back(converted);
        }
        return result;
    }

    std::unordered_map<std::uint32_t, RawStruct> raw_;
    std::unordered_map<std::uint32_t, std::uint32_t> aligns_;
};

Result<MetaSchema> readSchema(std::span<const std::byte> psch)
{
    if (psch.size() < 12)
        return failure("the schema section is cut short");
    const std::uint32_t count = be32(psch, 8);
    if (count > (psch.size() - 12) / 8)
        return failure("the schema section counts more types than it holds");
    MetaSchema schema;
    schema.bigEndian = true;
    schema.stringBlock = stringBlock;
    std::unordered_map<std::uint32_t, RawStruct> raw;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t hash = be32(psch, 12 + index * 8);
        const std::uint32_t at = be32(psch, 16 + index * 8);
        if (std::size_t(at) + 4 > psch.size())
            return failure("a type description lies outside the schema section");
        const std::uint32_t head = be32(psch, at);
        if ((head >> 24) == 0) {
            const std::uint32_t entries = head & 0xFFFF;
            std::size_t first = at + 12 + (((head >> 16) & 0xFF) == 1 ? 4 : 0);
            if (std::size_t(at) + 12 > psch.size() || first + entries * std::size_t(12) > psch.size())
                return failure("a structure description lies outside the schema section");
            RawStruct type { hash, be32(psch, at + 4), {} };
            for (std::uint32_t entry = 0; entry < entries; ++entry, first += 12) {
                type.entries.push_back(RawEntry { be32(psch, first), std::to_integer<std::uint8_t>(psch[first + 4]), std::to_integer<std::uint8_t>(psch[first + 5]), be16(psch, first + 6), be32(psch, first + 8) });
            }
            raw.emplace(hash, std::move(type));
        } else {
            const std::uint32_t entries = head & 0xFFFFFF;
            if (at + 4 + entries * std::size_t(8) > psch.size())
                return failure("an enumeration lies outside the schema section");
            MetaEnum values { hash, {} };
            for (std::uint32_t entry = 0; entry < entries; ++entry)
                values.values.emplace_back(be32(psch, at + 4 + entry * 8), static_cast<std::int32_t>(be32(psch, at + 8 + entry * 8)));
            schema.enums.emplace(hash, std::move(values));
        }
    }
    SchemaBuilder builder(std::move(raw));
    if (const auto built = builder.build(schema); !built)
        return std::unexpected(built.error());
    return schema;
}

}

bool isPsoFile(std::span<const std::byte> bytes)
{
    return bytes.size() >= 4 && std::memcmp(bytes.data(), "PSIN", 4) == 0;
}

Result<MetaData> readPsoFile(std::span<const std::byte> bytes)
{
    if (!isPsoFile(bytes))
        return failure("the file is not a PSO file");
    const auto list = sections(bytes);
    if (!list)
        return std::unexpected(list.error());
    const Section *psin = section(*list, "PSIN");
    const Section *pmap = section(*list, "PMAP");
    const Section *psch = section(*list, "PSCH");
    if (psin == nullptr || pmap == nullptr || psch == nullptr)
        return failure("the file lacks a PSIN, PMAP or PSCH section");
    auto schema = readSchema(psch->bytes);
    if (!schema)
        return std::unexpected(schema.error());
    MetaData data;
    data.schema = std::move(*schema);
    if (pmap->bytes.size() < 16)
        return failure("the block map is cut short");
    data.root = be32(pmap->bytes, 8);
    const std::uint16_t count = be16(pmap->bytes, 12);
    if (16 + count * std::size_t(16) > pmap->bytes.size())
        return failure("the block map counts more blocks than it holds");
    std::size_t copied = 0;
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::size_t at = 16 + index * std::size_t(16);
        const std::uint32_t offset = be32(pmap->bytes, at + 4);
        const std::uint32_t length = be32(pmap->bytes, at + 12);
        if (offset > psin->bytes.size() || length > psin->bytes.size() - offset)
            return failure("a block lies outside the data section");
        copied += length;
        if (copied > psin->bytes.size())
            return failure("the blocks overlap");
        const auto block = psin->bytes.subspan(offset, length);
        data.blocks.push_back(MetaBlock { be32(pmap->bytes, at), std::vector<std::byte>(block.begin(), block.end()) });
    }
    if (data.root == 0 || data.root > data.blocks.size())
        return failure("the root block does not exist");
    return data;
}

Result<std::vector<std::byte>> writePsoFile(std::span<const std::byte> original, const MetaData &data)
{
    const auto list = sections(original);
    if (!list)
        return std::unexpected(list.error());
    std::vector<std::byte> out { std::byte { 'P' }, std::byte { 'S' }, std::byte { 'I' }, std::byte { 'N' }, {}, {}, {}, {} };
    out.insert(out.end(), psinHeader - sectionHeader, fill);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> placed;
    for (const MetaBlock &block : data.blocks) {
        while (out.size() % blockAlignment != 0)
            out.push_back(fill);
        placed.emplace_back(static_cast<std::uint32_t>(out.size()), static_cast<std::uint32_t>(block.data.size()));
        out.insert(out.end(), block.data.begin(), block.data.end());
    }
    if (out.size() > 0xFFFFFFFFu)
        return failure("the data section would be larger than 4 GiB");
    const auto psinSize = static_cast<std::uint32_t>(out.size());
    for (int index = 0; index < 4; ++index)
        out[4 + index] = static_cast<std::byte>((psinSize >> (24 - 8 * index)) & 0xFF);
    const std::size_t pmapAt = out.size();
    out.insert(out.end(), { std::byte { 'P' }, std::byte { 'M' }, std::byte { 'A' }, std::byte { 'P' } });
    append32(out, static_cast<std::uint32_t>(16 + 16 * data.blocks.size()));
    append32(out, data.root);
    append16(out, static_cast<std::uint16_t>(data.blocks.size()));
    append16(out, 0x7070);
    for (std::size_t index = 0; index < data.blocks.size(); ++index) {
        append32(out, data.blocks[index].type);
        append32(out, placed[index].first);
        append32(out, 0);
        append32(out, placed[index].second);
    }
    const Section *psin = section(*list, "PSIN");
    const Section *pmap = section(*list, "PMAP");
    const bool unchanged = psin != nullptr && pmap != nullptr && std::ranges::equal(std::span(out).first(pmapAt), psin->bytes) && std::ranges::equal(std::span(out).subspan(pmapAt), pmap->bytes);
    for (const Section &kept : *list) {
        if (kept.name == "PSIN" || kept.name == "PMAP" || (kept.name == "CHKS" && !unchanged))
            continue;
        out.insert(out.end(), kept.bytes.begin(), kept.bytes.end());
    }
    return out;
}

}
