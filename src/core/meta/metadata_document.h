#pragma once

#include "core/io/error.h"
#include "core/meta/name_table.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace archon {

enum class MetadataFormat {
    Pso,
    RscMeta,
    Rbf,
};

struct MetadataDocument {
    MetadataFormat format;
    std::string xml;
    std::string readOnlyReason;
};

bool isBinaryMetadata(std::span<const std::byte> file);
Result<MetadataDocument> openMetadata(std::span<const std::byte> file, const NameTable &names);
Result<std::vector<std::byte>> saveMetadata(std::span<const std::byte> original, std::string_view xml, const NameTable &names);

}
