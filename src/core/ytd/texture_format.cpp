#include "core/ytd/texture_format.h"

#include <algorithm>
#include <array>

namespace archon {

namespace {

constexpr std::array<TextureFormatInfo, 13> formats { {
    { 0x31545844, "DXT1", true, 4 },
    { 0x33545844, "DXT3", true, 8 },
    { 0x35545844, "DXT5", true, 8 },
    { 0x31495441, "ATI1", true, 4 },
    { 0x32495441, "ATI2", true, 8 },
    { 0x20374342, "BC7", true, 8 },
    { 21, "A8R8G8B8", false, 32 },
    { 22, "X8R8G8B8", false, 32 },
    { 23, "R5G6B5", false, 16 },
    { 25, "A1R5G5B5", false, 16 },
    { 28, "A8", false, 8 },
    { 32, "A8B8G8R8", false, 32 },
    { 50, "L8", false, 8 },
} };

std::uint64_t levelSize(const TextureFormatInfo &info, std::uint32_t width, std::uint32_t height)
{
    if (info.block)
        return std::uint64_t(std::max(1u, (width + 3) / 4)) * std::max(1u, (height + 3) / 4) * info.bitsPerPixel * 2;
    return std::uint64_t(width) * height * info.bitsPerPixel / 8;
}

}

std::optional<TextureFormatInfo> textureFormat(std::uint32_t code)
{
    const auto found = std::ranges::find(formats, code, &TextureFormatInfo::code);
    return found == formats.end() ? std::nullopt : std::optional(*found);
}

std::optional<std::uint64_t> mipOffset(std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t level)
{
    const auto info = textureFormat(format);
    if (!info)
        return std::nullopt;
    std::uint64_t offset = 0;
    for (std::uint32_t index = 0; index < level; ++index)
        offset += levelSize(*info, std::max(1u, width >> index), std::max(1u, height >> index));
    return offset;
}

std::optional<std::uint64_t> mipChainSize(std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t levels)
{
    return mipOffset(format, width, height, levels);
}

std::uint32_t textureStride(std::uint32_t format, std::uint32_t width)
{
    const auto info = textureFormat(format);
    return info ? width * info->bitsPerPixel / 8 : 0;
}

}
