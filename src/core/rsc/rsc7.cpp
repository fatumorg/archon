#include "core/rsc/rsc7.h"

#include "core/io/byte_reader.h"

namespace archon {

namespace {

constexpr std::uint32_t magic = 0x37435352;

}

Result<Rsc7Header> parseRsc7Header(std::span<const std::byte> first16Bytes)
{
    ByteReader reader(first16Bytes, "rsc7");
    const auto fileMagic = reader.u32();
    const auto version = reader.u32();
    const auto systemFlags = reader.u32();
    const auto graphicsFlags = reader.u32();
    if (!fileMagic || !version || !systemFlags || !graphicsFlags)
        return std::unexpected(reader.error("header is truncated"));
    if (*fileMagic != magic)
        return std::unexpected(Error { "rsc7", 0, "not an RSC7 resource" });
    return Rsc7Header { *version, *systemFlags, *graphicsFlags };
}

std::array<std::byte, 16> buildRsc7Header(const Rsc7Header &header)
{
    std::array<std::byte, 16> bytes {};
    const std::array<std::uint32_t, 4> words { magic, header.version, header.systemFlags, header.graphicsFlags };
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::byte>(words[i / 4] >> (8 * (i % 4)));
    return bytes;
}

std::uint32_t rsc7VersionFromFlags(std::uint32_t systemFlags, std::uint32_t graphicsFlags)
{
    return ((graphicsFlags >> 28) & 0xF) | (((systemFlags >> 28) & 0xF) << 4);
}

std::uint64_t rsc7SizeFromFlags(std::uint32_t flags)
{
    const std::uint64_t base = std::uint64_t(0x200) << (flags & 0xF);
    const std::uint64_t count = ((flags >> 27) & 1)
        + (((flags >> 26) & 1) << 1)
        + (((flags >> 25) & 1) << 2)
        + (((flags >> 24) & 1) << 3)
        + (((flags >> 17) & 0x7F) << 4)
        + (((flags >> 11) & 0x3F) << 5)
        + (((flags >> 7) & 0xF) << 6)
        + (((flags >> 5) & 0x3) << 7)
        + (((flags >> 4) & 1) << 8);
    return base * count;
}

}
