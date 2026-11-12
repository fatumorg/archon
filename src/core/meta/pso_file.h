#pragma once

#include "core/io/error.h"
#include "core/meta/meta_schema.h"

#include <span>
#include <vector>

namespace archon {

bool isPsoFile(std::span<const std::byte> bytes);
Result<MetaData> readPsoFile(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> writePsoFile(std::span<const std::byte> original, const MetaData &data);

}
