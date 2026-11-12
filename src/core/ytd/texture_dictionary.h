#pragma once

#include "core/io/error.h"

#include <span>
#include <string>
#include <vector>

namespace archon {

struct Texture {
    std::string name;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t levels;
    std::uint32_t format;
    std::uint32_t usage;
    std::vector<std::byte> data;
    std::uint32_t enhancedFlags = 0x00260208;
    std::uint8_t enhancedMipFlags = 0;
};

inline constexpr std::uint32_t legacyDictionaryVersion = 13;
inline constexpr std::uint32_t enhancedDictionaryVersion = 5;

Result<std::vector<Texture>> readTextureDictionary(std::span<const std::byte> pages, std::uint64_t systemSize, std::uint32_t version = legacyDictionaryVersion);
Result<std::vector<std::byte>> writeTextureDictionary(std::span<const Texture> textures, std::uint32_t version = legacyDictionaryVersion);
std::uint32_t textureNameHash(std::string_view name);

}
