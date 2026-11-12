#pragma once

#include "core/ytd/texture_dictionary.h"

#include <string_view>

namespace archon {

bool isValidTextureName(std::string_view name);
Result<void> addTexture(std::vector<Texture> &textures, Texture texture);
Result<void> replaceTexture(std::vector<Texture> &textures, std::string_view name, Texture replacement);
Result<void> removeTexture(std::vector<Texture> &textures, std::string_view name);
Result<void> renameTexture(std::vector<Texture> &textures, std::string_view name, std::string_view newName);

}
