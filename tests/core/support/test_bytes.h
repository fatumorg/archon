#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace archon::test {

std::span<const std::byte> asBytes(std::string_view text);
std::vector<std::byte> toVector(std::string_view text);
std::vector<std::byte> fromHex(std::string_view hex);
std::string toHex(std::span<const std::byte> data);
std::string toText(std::span<const std::byte> data);
std::vector<std::byte> deflateRaw(std::span<const std::byte> data);

}
