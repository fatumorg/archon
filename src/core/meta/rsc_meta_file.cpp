#include "core/meta/rsc_meta_file.h"

#include "core/rsc/resource_file.h"
#include "core/rsc/rsc7.h"

#include <algorithm>
#include <optional>
#include <string>

namespace archon {

namespace {

constexpr std::uint64_t systemBase = 0x50000000;
constexpr std::uint64_t graphicsBase = 0x60000000;
constexpr std::uint32_t pseudoEntry = 0x100;
constexpr std::size_t headerSize = 0x70;
constexpr std::size_t structInfoSize = 0x20;
constexpr std::size_t entrySize = 0x10;
constexpr std::size_t enumInfoSize = 0x18;
constexpr std::size_t enumEntrySize = 8;
constexpr std::size_t blockInfoSize = 0x10;
constexpr std::size_t pagesInfoSize = 0x20;
constexpr std::uint32_t stringBlock = 0x10;
constexpr std::uint32_t blockCapacity = 0x4000;
constexpr std::uint32_t ownBlockFlag = 0x20;
constexpr std::uint32_t metaMagic = 0x50524430;

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "meta", 0, std::move(message) });
}

std::uint64_t le(std::span<const std::byte> data, std::size_t at, std::size_t size)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index)
        value |= std::to_integer<std::uint64_t>(data[at + index]) << (8 * index);
    return value;
}

void put(std::vector<std::byte> &data, std::size_t at, std::uint64_t value, std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
        data[at + index] = static_cast<std::byte>((value >> (8 * index)) & 0xFF);
}

std::size_t alignUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

class Pages {
public:
    Pages(std::span<const std::byte> pages, std::uint64_t systemSize, std::uint64_t graphicsSize)
        : pages_(pages)
        , systemSize_(std::min<std::uint64_t>(systemSize, pages.size()))
        , graphicsSize_(std::min<std::uint64_t>(graphicsSize, pages.size() - systemSize_))
    {
    }

    std::optional<std::size_t> resolve(std::uint64_t pointer, std::uint64_t length) const
    {
        if (pointer >= graphicsBase && pointer - graphicsBase <= graphicsSize_ && length <= graphicsSize_ - (pointer - graphicsBase))
            return static_cast<std::size_t>(systemSize_ + pointer - graphicsBase);
        if (pointer < systemBase || pointer >= graphicsBase || pointer - systemBase > systemSize_ || length > systemSize_ - (pointer - systemBase))
            return std::nullopt;
        return static_cast<std::size_t>(pointer - systemBase);
    }

    bool inGraphics(std::uint64_t pointer) const
    {
        return pointer >= graphicsBase;
    }

    std::uint64_t value(std::size_t at, std::size_t size) const
    {
        return le(pages_, at, size);
    }

    std::span<const std::byte> bytes() const
    {
        return pages_;
    }

private:
    std::span<const std::byte> pages_;
    std::uint64_t systemSize_;
    std::uint64_t graphicsSize_;
};

struct RawEntry {
    std::uint32_t name;
    std::int32_t offset;
    std::uint8_t type;
    std::uint8_t flags;
    std::int16_t element;
    std::uint32_t reference;
};

struct RawStruct {
    std::uint32_t name;
    std::uint32_t alignFlag;
    std::uint32_t size;
    std::vector<RawEntry> entries;
};

struct Header {
    std::uint64_t structs;
    std::uint64_t enums;
    std::uint64_t blocks;
    std::uint64_t name;
    std::uint64_t extra;
    std::uint32_t structCount;
    std::uint32_t enumCount;
    std::uint32_t blockCount;
    std::uint32_t root;
};

Header readHeader(const Pages &pages)
{
    return Header { pages.value(0x20, 8), pages.value(0x28, 8), pages.value(0x30, 8), pages.value(0x38, 8), pages.value(0x40, 8), static_cast<std::uint32_t>(pages.value(0x48, 2)), static_cast<std::uint32_t>(pages.value(0x4A, 2)), static_cast<std::uint32_t>(pages.value(0x4C, 2)), static_cast<std::uint32_t>(pages.value(0x1C, 4)) };
}

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
        switch (type->second.alignFlag) {
        case 0x100:
        case 0x400:
            align = 16;
            break;
        case 0x200:
            align = 4;
            break;
        case 0x300:
            align = 8;
            break;
        default:
            for (const RawEntry &entry : type->second.entries) {
                if (entry.name == pseudoEntry)
                    continue;
                const auto field = convert(type->second, entry, depth + 1);
                if (!field)
                    return std::unexpected(field.error());
                align = std::max(align, field->align);
            }
            break;
        }
        aligns_[hash] = align;
        return align;
    }

    static std::uint32_t blockType(const RawEntry &entry)
    {
        switch (entry.type) {
        case 0x05:
            return entry.reference;
        case 0x01:
        case 0x60:
            return 0x11;
        case 0x64:
            return 0x13;
        case 0x62:
        case 0x63:
        case 0x65:
            return 0x15;
        default:
            return entry.type;
        }
    }

    Result<MetaField> convert(const RawStruct &owner, const RawEntry &entry, int depth)
    {
        MetaField field;
        field.name = entry.name;
        field.offset = static_cast<std::uint32_t>(std::max(entry.offset, 0));
        field.blockType = blockType(entry);
        const auto simple = [&field](MetaType type, std::uint32_t size, std::uint32_t align) {
            field.type = type;
            field.size = size;
            field.align = align;
            return field;
        };
        switch (entry.type) {
        case 0x01:
            return simple(MetaType::Bool, 1, 1);
        case 0x10:
            return simple(MetaType::Int8, 1, 1);
        case 0x11:
            return simple(MetaType::UInt8, 1, 1);
        case 0x12:
            return simple(MetaType::Int16, 2, 2);
        case 0x13:
            return simple(MetaType::UInt16, 2, 2);
        case 0x14:
            return simple(MetaType::Int32, 4, 4);
        case 0x15:
            return simple(MetaType::UInt32, 4, 4);
        case 0x21:
            return simple(MetaType::Float, 4, 4);
        case 0x33:
            return simple(MetaType::Vector3, 16, 16);
        case 0x34:
            return simple(MetaType::Vector4, 16, 16);
        case 0x40:
            return simple(MetaType::FixedString, entry.reference, 1);
        case 0x44:
            return simple(MetaType::AtString, 16, 8);
        case 0x4A:
            return simple(MetaType::Hash, 4, 4);
        case 0x59:
            return simple(MetaType::Raw, 8, 8);
        case 0x05: {
            field.reference = entry.reference;
            const auto type = raw_.find(entry.reference);
            const auto align = structAlign(entry.reference, depth);
            if (!align)
                return std::unexpected(align.error());
            return simple(MetaType::Struct, type == raw_.end() ? 0 : type->second.size, *align);
        }
        case 0x07:
            return simple(MetaType::StructPointer, 8, 8);
        case 0x50:
        case 0x52: {
            if (entry.element < 0 || static_cast<std::size_t>(entry.element) >= owner.entries.size())
                return failure("an array names an item description that does not exist");
            field.element = entry.element;
            if (entry.type == 0x52) {
                field.ownBlock = (entry.flags & ownBlockFlag) != 0;
                return simple(MetaType::Array, field.ownBlock ? 24 : 16, field.ownBlock ? 16 : 8);
            }
            if (depth > 32)
                return failure("arrays contain each other");
            const auto item = convert(owner, owner.entries[static_cast<std::size_t>(entry.element)], depth + 1);
            if (!item)
                return std::unexpected(item.error());
            field.count = entry.reference;
            return simple(MetaType::FixedArray, entry.reference * item->size, item->align);
        }
        case 0x60:
            field.reference = entry.reference;
            return simple(MetaType::Enum, 1, 1);
        case 0x62:
            field.reference = entry.reference;
            return simple(MetaType::Enum, 4, 4);
        case 0x63:
        case 0x65:
            field.reference = entry.reference;
            return simple(MetaType::Flags, 4, 4);
        case 0x64:
            field.reference = entry.reference;
            return simple(MetaType::Flags, 2, 2);
        default:
            return failure("a field uses type " + std::to_string(entry.type) + ", which ARCHON does not read");
        }
    }

    Result<MetaStruct> convertStruct(const RawStruct &type)
    {
        MetaStruct result;
        result.name = type.name;
        result.size = type.size;
        const auto align = structAlign(type.name, 0);
        if (!align)
            return std::unexpected(align.error());
        result.align = *align;
        for (const RawEntry &entry : type.entries) {
            const auto field = convert(type, entry, 0);
            if (!field && entry.name != pseudoEntry)
                return std::unexpected(field.error());
            if (entry.name != pseudoEntry) {
                if (entry.offset < 0 || static_cast<std::uint64_t>(entry.offset) + field->size > type.size)
                    return failure("a field lies outside its structure");
                result.members.push_back(result.entries.size());
            }
            result.entries.push_back(field ? *field : MetaField {});
        }
        return result;
    }

    std::unordered_map<std::uint32_t, RawStruct> raw_;
    std::unordered_map<std::uint32_t, std::uint32_t> aligns_;
};

std::size_t extraSize(const Pages &pages, std::size_t at)
{
    const auto bytes = pages.bytes();
    const std::uint64_t length = std::to_integer<std::uint64_t>(bytes[at]) << 24 | std::to_integer<std::uint64_t>(bytes[at + 1]) << 16 | std::to_integer<std::uint64_t>(bytes[at + 2]) << 8 | std::to_integer<std::uint64_t>(bytes[at + 3]);
    return static_cast<std::size_t>(length + 4);
}

}

Result<MetaData> readRscMeta(const RscPages &resource)
{
    const Pages pages(resource.pages, rsc7SizeFromFlags(resource.systemFlags), rsc7SizeFromFlags(resource.graphicsFlags));
    if (!pages.resolve(systemBase, headerSize))
        return failure("the resource is smaller than a metadata header");
    if (pages.value(0x10, 4) != metaMagic)
        return failure("the resource is not metadata");
    const Header header = readHeader(pages);
    const auto structs = pages.resolve(header.structs, header.structCount * structInfoSize);
    const auto blocks = pages.resolve(header.blocks, header.blockCount * blockInfoSize);
    const auto enums = header.enumCount == 0 ? std::optional<std::size_t>(0) : pages.resolve(header.enums, header.enumCount * enumInfoSize);
    if (!structs || !blocks || !enums)
        return failure("a metadata table lies outside the resource");
    std::unordered_map<std::uint32_t, RawStruct> raw;
    for (std::uint32_t index = 0; index < header.structCount; ++index) {
        const std::size_t at = *structs + index * structInfoSize;
        RawStruct type { static_cast<std::uint32_t>(pages.value(at, 4)), static_cast<std::uint32_t>(pages.value(at + 8, 4)), static_cast<std::uint32_t>(pages.value(at + 0x18, 4)), {} };
        const std::uint32_t count = static_cast<std::uint32_t>(pages.value(at + 0x1E, 2));
        if (count > 0) {
            const auto entries = pages.resolve(pages.value(at + 0x10, 8), count * entrySize);
            if (!entries)
                return failure("a structure's fields lie outside the resource");
            for (std::uint32_t entry = 0; entry < count; ++entry) {
                const std::size_t e = *entries + entry * entrySize;
                type.entries.push_back(RawEntry { static_cast<std::uint32_t>(pages.value(e, 4)), static_cast<std::int32_t>(pages.value(e + 4, 4)), static_cast<std::uint8_t>(pages.value(e + 8, 1)), static_cast<std::uint8_t>(pages.value(e + 9, 1)), static_cast<std::int16_t>(pages.value(e + 10, 2)), static_cast<std::uint32_t>(pages.value(e + 12, 4)) });
            }
        }
        raw.emplace(type.name, std::move(type));
    }
    MetaData data;
    data.schema.bigEndian = false;
    data.schema.stringBlock = stringBlock;
    data.schema.blockCapacity = blockCapacity;
    for (std::uint32_t index = 0; index < header.enumCount; ++index) {
        const std::size_t at = *enums + index * enumInfoSize;
        MetaEnum values { static_cast<std::uint32_t>(pages.value(at, 4)), {} };
        const auto count = static_cast<std::uint32_t>(pages.value(at + 0x10, 4));
        if (count > 0) {
            const auto entries = pages.resolve(pages.value(at + 8, 8), std::uint64_t(count) * enumEntrySize);
            if (!entries)
                return failure("an enumeration lies outside the resource");
            for (std::uint32_t entry = 0; entry < count; ++entry)
                values.values.emplace_back(static_cast<std::uint32_t>(pages.value(*entries + entry * enumEntrySize, 4)), static_cast<std::int32_t>(pages.value(*entries + entry * enumEntrySize + 4, 4)));
        }
        data.schema.enums.emplace(values.name, std::move(values));
    }
    SchemaBuilder builder(std::move(raw));
    if (const auto built = builder.build(data.schema); !built)
        return std::unexpected(built.error());
    for (std::uint32_t index = 0; index < header.blockCount; ++index) {
        const std::size_t at = *blocks + index * blockInfoSize;
        const std::uint64_t length = pages.value(at + 4, 4);
        const auto location = pages.resolve(pages.value(at + 8, 8), length);
        if (!location)
            return failure("a data block lies outside the resource");
        const auto bytes = pages.bytes().subspan(*location, static_cast<std::size_t>(length));
        data.blocks.push_back(MetaBlock { static_cast<std::uint32_t>(pages.value(at, 4)), std::vector<std::byte>(bytes.begin(), bytes.end()), pages.inGraphics(pages.value(at + 8, 8)) });
    }
    data.root = header.root;
    if (data.root == 0 || data.root > data.blocks.size())
        return failure("the root block does not exist");
    return data;
}

Result<RscPages> writeRscMeta(const RscPages &original, const MetaData &data)
{
    const Pages pages(original.pages, rsc7SizeFromFlags(original.systemFlags), rsc7SizeFromFlags(original.graphicsFlags));
    const auto old = readRscMeta(original);
    if (!old)
        return std::unexpected(old.error());
    const Header header = readHeader(pages);
    const auto sameShape = std::ranges::equal(old->blocks, data.blocks, [](const MetaBlock &a, const MetaBlock &b) { return a.type == b.type && a.data.size() == b.data.size() && a.graphics == b.graphics; }) && old->root == data.root;
    if (sameShape) {
        RscPages result = original;
        for (std::uint32_t index = 0; index < header.blockCount; ++index) {
            const std::size_t at = *pages.resolve(header.blocks, header.blockCount * blockInfoSize) + index * blockInfoSize;
            const std::size_t location = *pages.resolve(pages.value(at + 8, 8), 0);
            std::ranges::copy(data.blocks[index].data, result.pages.begin() + static_cast<std::ptrdiff_t>(location));
        }
        return result;
    }
    std::vector<std::byte> out;
    const auto place = [&out](std::size_t size, std::size_t alignment) {
        const std::size_t at = alignUp(out.size(), alignment);
        out.resize(at + size);
        return at;
    };
    const auto copy = [&out, &pages](std::size_t to, std::size_t from, std::size_t size) { std::ranges::copy(pages.bytes().subspan(from, size), out.begin() + static_cast<std::ptrdiff_t>(to)); };
    place(headerSize, 16);
    copy(0, 0, headerSize);
    const std::size_t structsFrom = *pages.resolve(header.structs, header.structCount * structInfoSize);
    const std::size_t structsAt = place(header.structCount * structInfoSize, 16);
    copy(structsAt, structsFrom, header.structCount * structInfoSize);
    for (std::uint32_t index = 0; index < header.structCount; ++index) {
        const std::size_t info = structsAt + index * structInfoSize;
        const std::size_t count = le(out, info + 0x1E, 2);
        if (count == 0)
            continue;
        const std::size_t from = *pages.resolve(le(out, info + 0x10, 8), count * entrySize);
        const std::size_t to = place(count * entrySize, 16);
        copy(to, from, count * entrySize);
        put(out, info + 0x10, systemBase + to, 8);
    }
    std::size_t enumsAt = 0;
    if (header.enumCount > 0) {
        enumsAt = place(header.enumCount * enumInfoSize, 16);
        copy(enumsAt, *pages.resolve(header.enums, header.enumCount * enumInfoSize), header.enumCount * enumInfoSize);
        for (std::uint32_t index = 0; index < header.enumCount; ++index) {
            const std::size_t info = enumsAt + index * enumInfoSize;
            const std::size_t count = le(out, info + 0x10, 4);
            if (count == 0)
                continue;
            const std::size_t from = *pages.resolve(le(out, info + 8, 8), count * enumEntrySize);
            const std::size_t to = place(count * enumEntrySize, 16);
            copy(to, from, count * enumEntrySize);
            put(out, info + 8, systemBase + to, 8);
        }
    }
    std::vector<std::byte> graphics;
    std::vector<std::uint64_t> blockAt;
    for (const MetaBlock &block : data.blocks) {
        if (block.graphics) {
            const std::size_t at = alignUp(graphics.size(), 16);
            graphics.resize(at + block.data.size());
            std::ranges::copy(block.data, graphics.begin() + static_cast<std::ptrdiff_t>(at));
            blockAt.push_back(graphicsBase + at);
            continue;
        }
        const std::size_t at = place(block.data.size(), 16);
        std::ranges::copy(block.data, out.begin() + static_cast<std::ptrdiff_t>(at));
        blockAt.push_back(systemBase + at);
    }
    const std::size_t tableAt = place(data.blocks.size() * blockInfoSize, 16);
    for (std::size_t index = 0; index < data.blocks.size(); ++index) {
        put(out, tableAt + index * blockInfoSize, data.blocks[index].type, 4);
        put(out, tableAt + index * blockInfoSize + 4, data.blocks[index].data.size(), 4);
        put(out, tableAt + index * blockInfoSize + 8, blockAt[index], 8);
    }
    std::size_t nameAt = 0;
    if (header.name != 0) {
        const auto from = pages.resolve(header.name, 1);
        if (!from)
            return failure("the metadata name lies outside the resource");
        const auto rest = pages.bytes().subspan(*from);
        const std::size_t length = static_cast<std::size_t>(std::ranges::find(rest, std::byte { 0 }) - rest.begin()) + 1;
        if (length > rest.size())
            return failure("the metadata name is not terminated");
        nameAt = place(length, 16);
        copy(nameAt, *from, length);
    }
    std::size_t extraAt = 0;
    if (header.extra != 0) {
        const auto from = pages.resolve(header.extra, 4);
        if (!from || !pages.resolve(header.extra, extraSize(pages, *from)))
            return failure("the metadata name table lies outside the resource");
        const std::size_t size = extraSize(pages, *from);
        extraAt = place(size, 16);
        copy(extraAt, *from, size);
    }
    const std::size_t pagesAt = place(pagesInfoSize, 16);
    out[pagesAt + 8] = std::byte { 1 };
    put(out, 0x08, systemBase + pagesAt, 8);
    put(out, 0x1C, data.root, 4);
    put(out, 0x20, systemBase + structsAt, 8);
    put(out, 0x28, header.enumCount > 0 ? systemBase + enumsAt : 0, 8);
    put(out, 0x30, systemBase + tableAt, 8);
    put(out, 0x38, header.name != 0 ? systemBase + nameAt : 0, 8);
    put(out, 0x40, header.extra != 0 ? systemBase + extraAt : 0, 8);
    put(out, 0x4C, data.blocks.size(), 2);
    const std::uint64_t size = singlePageSize(out.size());
    out.resize(static_cast<std::size_t>(size));
    const std::uint32_t version = (original.graphicsFlags >> 28) & 0xF;
    std::uint32_t graphicsFlags = version << 28;
    if (!graphics.empty()) {
        const std::uint64_t graphicsSize = singlePageSize(graphics.size());
        graphics.resize(static_cast<std::size_t>(graphicsSize));
        graphicsFlags = singlePageFlags(graphicsSize, version);
        out.insert(out.end(), graphics.begin(), graphics.end());
    }
    return RscPages { singlePageFlags(size, (original.systemFlags >> 28) & 0xF), graphicsFlags, std::move(out) };
}

}
