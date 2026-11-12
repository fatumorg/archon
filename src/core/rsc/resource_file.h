#pragma once

#include "core/io/error.h"

#include <span>
#include <vector>

namespace archon {

std::uint32_t singlePageFlags(std::uint64_t size, std::uint32_t versionNibble);
std::uint64_t singlePageSize(std::uint64_t content);
Result<std::vector<std::byte>> buildResourceFile(std::uint32_t version, std::span<const std::byte> system, std::span<const std::byte> graphics);
Result<std::vector<std::byte>> inflateResource(std::span<const std::byte> file);

}
