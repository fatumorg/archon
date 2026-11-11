#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace archon {

std::uint32_t jenkinsHash(std::span<const std::byte> data);
std::array<std::byte, 20> sha1(std::span<const std::byte> data);
std::array<std::byte, 32> sha256(std::span<const std::byte> data);

}
