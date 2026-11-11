#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace archon {

std::optional<std::filesystem::path> resolveCaseInsensitive(const std::filesystem::path &root, std::string_view relativePath);

}
