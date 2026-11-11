#pragma once

#include "core/io/error.h"

#include <span>
#include <vector>

namespace archon {

Result<std::vector<std::byte>> inflateRaw(std::span<const std::byte> data, std::size_t exactSize);

}
