#include "core/rpf/rpf_toc.h"

#include "core/io/byte_writer.h"

#include <map>

namespace archon {

namespace {

constexpr char format[] = "rpf7";
constexpr std::uint32_t magic = 0x52504637;
constexpr std::uint32_t openTag = 0x4E45504F;
constexpr std::uint32_t directoryMarker = 0x7FFFFF00;
constexpr std::uint32_t resourceBit = 0x800000;
constexpr std::uint32_t maxBlock = 0x7FFFFF;
constexpr std::uint32_t maxFileNameOffset = 0xFFFF;
constexpr std::size_t maxNameLength = 255;

std::unexpected<Error> failure(const char *message)
{
    return std::unexpected(Error { format, 0, message });
}

}

bool isValidEntryName(std::string_view name)
{
    return !name.empty()
        && name.size() <= maxNameLength
        && name != "."
        && name != ".."
        && name.find_first_of(std::string_view("/\\\0", 3)) == std::string_view::npos;
}

Result<std::vector<std::byte>> serializeToc(const std::vector<RpfEntry> &entries)
{
    std::vector<std::byte> names;
    std::map<std::string, std::uint32_t> known;
    std::vector<std::uint32_t> nameOffsets(entries.size(), 0);
    for (const bool directories : { false, true }) {
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const RpfEntry &entry = entries[index];
            if ((entry.kind == RpfEntryKind::Directory) != directories)
                continue;
            const auto [place, added] = known.try_emplace(entry.name, static_cast<std::uint32_t>(names.size()));
            if (added) {
                putBytes(names, std::as_bytes(std::span(entry.name.data(), entry.name.size())));
                names.push_back(std::byte { 0 });
            }
            if (!directories && place->second > maxFileNameOffset)
                return failure("archive holds too many file names");
            nameOffsets[index] = place->second;
        }
    }
    names.resize((names.size() + 15) / 16 * 16);

    std::vector<std::byte> out;
    out.reserve(16 + entries.size() * 16 + names.size());
    putInteger(out, magic, 4);
    putInteger(out, entries.size(), 4);
    putInteger(out, names.size(), 4);
    putInteger(out, openTag, 4);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const RpfEntry &entry = entries[index];
        if (entry.kind == RpfEntryKind::Directory) {
            putInteger(out, nameOffsets[index], 4);
            putInteger(out, directoryMarker, 4);
            putInteger(out, entry.firstChild, 4);
            putInteger(out, entry.childCount, 4);
            continue;
        }
        const std::uint64_t block = entry.offset / rpfBlockSize;
        if (entry.offset % rpfBlockSize != 0 || entry.storedSize > rpfSizeInHeaderMarker)
            return failure("entry cannot be stored");
        if (block > maxBlock)
            return failure("archive would exceed 4 GB; rebuild it first");
        const bool resource = entry.kind == RpfEntryKind::Resource;
        putInteger(out, nameOffsets[index], 2);
        putInteger(out, entry.storedSize, 3);
        putInteger(out, resource ? block | resourceBit : block, 3);
        putInteger(out, resource ? entry.systemFlags : entry.uncompressedSize, 4);
        putInteger(out, resource ? entry.graphicsFlags : (entry.encrypted ? 1u : 0u), 4);
    }
    putBytes(out, names);
    return out;
}

}
