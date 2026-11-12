#pragma once

#include "core/io/error.h"
#include "core/ytd/texture_dictionary.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace archon {

struct MeshVertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 2> uv;
};

struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string diffuse;
};

struct Drawable {
    std::uint32_t nameHash = 0;
    std::vector<Mesh> meshes;
    std::vector<Texture> textures;
};

bool isDrawableName(std::string_view name);
Result<std::vector<Drawable>> readDrawables(std::span<const std::byte> file, std::string_view name);

}
