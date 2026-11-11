#pragma once

#include "core/crypto/keys.h"

#include <string_view>
#include <vector>

namespace archon {

std::uint32_t ngHash(const Keys &keys, std::string_view lowerCaseName);
std::vector<std::byte> ngDecrypt(const Keys &keys, std::span<const std::byte> data, std::string_view name, std::uint32_t length);

}
