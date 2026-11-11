#pragma once

#include "core/rpf/rpf_reader.h"

namespace archon {

bool isSafeEntryName(std::string_view name);

Result<std::filesystem::path> extractEntry(const RpfReader &reader, std::uint32_t entryIndex, const std::filesystem::path &outputDirectory);

struct ExtractSummary {
    std::uint64_t files;
    std::uint64_t bytes;
    std::vector<Error> failures;
};

Result<ExtractSummary> extractTree(const RpfReader &reader, std::uint32_t directoryIndex, const std::filesystem::path &outputDirectory);

}
