#pragma once

#include "core/io/error.h"

#include <span>
#include <vector>

namespace archon {

Result<std::vector<std::byte>> aesEcbDecrypt(std::span<const std::byte> data, std::span<const std::byte, 32> key);

}
