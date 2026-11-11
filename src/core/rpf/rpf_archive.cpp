#include "core/rpf/rpf_archive.h"

#include "core/crypto/aes.h"
#include "core/crypto/ng.h"
#include "core/io/ascii.h"
#include "core/io/byte_reader.h"

namespace archon {

namespace {

constexpr char format[] = "rpf7";
constexpr std::uint32_t magic = 0x52504637;
constexpr std::uint32_t openTag = 0x4E45504F;
constexpr std::uint32_t aesTag = 0x0FFFFFF9;
constexpr std::uint32_t directoryMarker = 0x7FFFFF00;
constexpr std::uint32_t maxEntries = 1'000'000;
constexpr std::uint32_t maxNamesLength = 16u << 20;
constexpr std::uint32_t maxDirectoryDepth = 64;
constexpr std::size_t headerSize = 16;
constexpr std::size_t entrySize = 16;
constexpr std::uint64_t blockSize = 512;

struct Header {
    std::uint32_t entryCount;
    std::uint32_t namesLength;
    RpfEncryption encryption;
};

Result<Header> readHeader(ByteReader &reader)
{
    const auto fileMagic = reader.u32();
    if (!fileMagic)
        return std::unexpected(fileMagic.error());
    if (*fileMagic != magic)
        return std::unexpected(Error { format, 0, "not an RPF7 archive" });
    const auto entryCount = reader.u32();
    const auto namesLength = reader.u32();
    const auto tag = reader.u32();
    if (!entryCount || !namesLength || !tag)
        return std::unexpected(reader.error("header is truncated"));
    if (*entryCount == 0 || *entryCount > maxEntries)
        return std::unexpected(Error { format, 4, "entry count out of range" });
    if (*namesLength == 0 || *namesLength > maxNamesLength)
        return std::unexpected(Error { format, 8, "names length out of range" });
    const RpfEncryption encryption = *tag == 0 ? RpfEncryption::None
        : *tag == openTag ? RpfEncryption::Open
        : *tag == aesTag ? RpfEncryption::Aes
        : RpfEncryption::Ng;
    return Header { *entryCount, *namesLength, encryption };
}

Result<std::vector<std::byte>> decrypt(std::span<const std::byte> data, RpfEncryption encryption, std::string_view lowerName, std::uint64_t archiveSize, const Keys *keys)
{
    if (encryption == RpfEncryption::None || encryption == RpfEncryption::Open)
        return std::vector<std::byte>(data.begin(), data.end());
    if (keys == nullptr)
        return std::unexpected(Error { format, 12, "archive is encrypted and no keys are loaded" });
    if (encryption == RpfEncryption::Aes)
        return aesEcbDecrypt(data, keys->aes);
    return ngDecrypt(*keys, data, lowerName, static_cast<std::uint32_t>(archiveSize));
}

Result<RpfEntry> readEntry(ByteReader &table, const ByteReader &names, std::uint64_t archiveSize)
{
    const std::size_t start = table.position();
    const auto low = table.u32();
    const auto high = table.u32();
    const auto third = table.u32();
    const auto fourth = table.u32();
    if (!low || !high || !third || !fourth)
        return std::unexpected(table.error("entry table is truncated"));
    const auto failure = [start](const char *message) { return std::unexpected(Error { format, headerSize + start, message }); };

    RpfEntry entry {};
    const bool directory = *high == directoryMarker;
    const auto name = names.cString(directory ? *low : *low & 0xFFFF);
    if (!name)
        return failure("entry name lies outside the names block");
    entry.name = *name;
    if (directory) {
        entry.kind = RpfEntryKind::Directory;
        entry.firstChild = *third;
        entry.childCount = *fourth;
        return entry;
    }
    entry.storedSize = (*low >> 16) | ((*high & 0xFF) << 16);
    const std::uint32_t blocks = *high >> 8;
    if ((*high & 0x80000000) == 0) {
        entry.kind = RpfEntryKind::Binary;
        entry.offset = blocks * blockSize;
        entry.uncompressedSize = *third;
        entry.encrypted = *fourth == 1;
    } else {
        entry.kind = RpfEntryKind::Resource;
        entry.offset = (blocks & 0x7FFFFF) * blockSize;
        entry.systemFlags = *third;
        entry.graphicsFlags = *fourth;
    }
    const std::uint64_t occupied = entry.storedSize != 0 ? entry.storedSize : entry.uncompressedSize;
    if (entry.offset > archiveSize || occupied > archiveSize - entry.offset)
        return failure("entry data lies past the end of the archive");
    return entry;
}

Result<void> linkTree(std::vector<RpfEntry> &entries)
{
    const auto failure = [](std::uint32_t index, const char *message) { return std::unexpected(Error { format, headerSize + index * entrySize, message }); };
    if (entries[0].kind != RpfEntryKind::Directory)
        return failure(0, "first entry is not a directory");
    const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
    std::vector<bool> reached(count, false);
    std::vector<std::uint32_t> pending { 0 };
    std::vector<std::uint32_t> depth(count, 0);
    reached[0] = true;
    while (!pending.empty()) {
        const std::uint32_t directory = pending.back();
        pending.pop_back();
        const RpfEntry &parent = entries[directory];
        if (parent.firstChild > count || parent.childCount > count - parent.firstChild)
            return failure(directory, "directory children lie outside the entry table");
        for (std::uint32_t child = parent.firstChild; child < parent.firstChild + parent.childCount; ++child) {
            if (reached[child])
                return failure(directory, "entry belongs to more than one directory");
            reached[child] = true;
            entries[child].parent = directory;
            if (entries[child].kind != RpfEntryKind::Directory)
                continue;
            depth[child] = depth[directory] + 1;
            if (depth[child] > maxDirectoryDepth)
                return failure(child, "directories are nested too deep");
            pending.push_back(child);
        }
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        if (!reached[index])
            return failure(index, "entry is not part of any directory");
    }
    return {};
}

}

Result<std::size_t> RpfArchive::tocSize(std::span<const std::byte> first16Bytes)
{
    ByteReader reader(first16Bytes, format);
    const auto header = readHeader(reader);
    if (!header)
        return std::unexpected(header.error());
    return headerSize + std::size_t(header->entryCount) * entrySize + header->namesLength;
}

Result<RpfArchive> RpfArchive::parse(std::span<const std::byte> headerAndToc, std::string_view archiveName, std::uint64_t archiveSize, const Keys *keys)
{
    ByteReader reader(headerAndToc, format);
    const auto header = readHeader(reader);
    if (!header)
        return std::unexpected(header.error());
    const auto rawTable = reader.bytes(std::size_t(header->entryCount) * entrySize);
    if (!rawTable)
        return std::unexpected(reader.error("entry table is truncated"));
    const auto rawNames = reader.bytes(header->namesLength);
    if (!rawNames)
        return std::unexpected(reader.error("names block is truncated"));

    const std::string lowerName = asciiLower(archiveName);
    const auto table = decrypt(*rawTable, header->encryption, lowerName, archiveSize, keys);
    if (!table)
        return std::unexpected(table.error());
    const auto names = decrypt(*rawNames, header->encryption, lowerName, archiveSize, keys);
    if (!names)
        return std::unexpected(names.error());

    RpfArchive archive;
    archive.encryption_ = header->encryption;
    archive.entries_.reserve(header->entryCount);
    ByteReader tableReader(*table, format);
    const ByteReader namesReader(*names, format);
    for (std::uint32_t index = 0; index < header->entryCount; ++index) {
        auto entry = readEntry(tableReader, namesReader, archiveSize);
        if (!entry)
            return std::unexpected(entry.error());
        archive.entries_.push_back(std::move(*entry));
    }
    if (const auto linked = linkTree(archive.entries_); !linked)
        return std::unexpected(linked.error());
    return archive;
}

RpfEncryption RpfArchive::encryption() const
{
    return encryption_;
}

const std::vector<RpfEntry> &RpfArchive::entries() const
{
    return entries_;
}

std::optional<std::uint32_t> RpfArchive::find(std::string_view path) const
{
    std::uint32_t current = 0;
    while (!path.empty()) {
        const RpfEntry &directory = entries_[current];
        if (directory.kind != RpfEntryKind::Directory)
            return std::nullopt;
        const std::size_t separator = path.find('/');
        const std::string_view component = path.substr(0, separator);
        path = separator == std::string_view::npos ? std::string_view() : path.substr(separator + 1);
        std::optional<std::uint32_t> match;
        for (std::uint32_t child = directory.firstChild; child < directory.firstChild + directory.childCount; ++child) {
            if (asciiEqualIgnoreCase(entries_[child].name, component)) {
                match = child;
                break;
            }
        }
        if (!match)
            return std::nullopt;
        current = *match;
    }
    return current;
}

std::string RpfArchive::path(std::uint32_t index) const
{
    std::string result;
    while (index != 0 && index < entries_.size()) {
        result = result.empty() ? entries_[index].name : entries_[index].name + '/' + result;
        index = entries_[index].parent;
    }
    return result;
}

}
