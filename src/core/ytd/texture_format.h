#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace archon {

struct TextureFormatInfo {
    std::uint32_t code;
    std::string_view name;
    bool block;
    std::uint32_t bitsPerPixel;
};

std::optional<TextureFormatInfo> textureFormat(std::uint32_t code);
std::optional<std::uint64_t> mipChainSize(std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t levels);
std::optional<std::uint64_t> mipOffset(std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t level);
std::uint32_t textureStride(std::uint32_t format, std::uint32_t width);
std::uint32_t withoutSrgb(std::uint32_t format);

}
