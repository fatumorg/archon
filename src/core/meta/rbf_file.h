#pragma once

#include "core/io/error.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace archon {

bool isRbfFile(std::span<const std::byte> bytes);
Result<std::string> rbfToXml(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> rbfFromXml(std::string_view xml);

}
