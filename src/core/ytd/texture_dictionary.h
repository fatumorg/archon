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
};

Result<std::vector<Texture>> readTextureDictionary(std::span<const std::byte> pages, std::uint64_t systemSize);
Result<std::vector<std::byte>> writeTextureDictionary(std::span<const Texture> textures);
std::uint32_t textureNameHash(std::string_view name);

}
