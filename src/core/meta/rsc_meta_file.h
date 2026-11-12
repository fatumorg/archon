#pragma once

#include "core/io/error.h"
#include "core/meta/meta_schema.h"

#include <span>
#include <vector>

namespace archon {

inline constexpr std::uint32_t rscMetaVersion = 2;

struct RscPages {
    std::uint32_t systemFlags = 0;
    std::uint32_t graphicsFlags = 0;
    std::vector<std::byte> pages;
};

Result<MetaData> readRscMeta(const RscPages &resource);
Result<RscPages> writeRscMeta(const RscPages &original, const MetaData &data);

}
