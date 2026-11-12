#pragma once

#include "core/ytd/texture_dictionary.h"

namespace archon {

Result<std::vector<std::byte>> decodeRgba(const Texture &texture, std::uint32_t level);
Result<std::vector<std::byte>> writeDds(const Texture &texture);
Result<Texture> readDds(std::span<const std::byte> bytes, std::string name);
Texture rgbaTexture(std::string name, std::uint32_t width, std::uint32_t height, std::span<const std::byte> rgba);

}
