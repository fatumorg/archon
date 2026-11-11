#pragma once

#include "core/io/error.h"

#include <array>
#include <span>

namespace archon {

struct Rsc7Header {
    std::uint32_t version;
    std::uint32_t systemFlags;
    std::uint32_t graphicsFlags;
};

Result<Rsc7Header> parseRsc7Header(std::span<const std::byte> first16Bytes);
std::array<std::byte, 16> buildRsc7Header(const Rsc7Header &header);
std::uint32_t rsc7VersionFromFlags(std::uint32_t systemFlags, std::uint32_t graphicsFlags);
std::uint64_t rsc7SizeFromFlags(std::uint32_t flags);

}
