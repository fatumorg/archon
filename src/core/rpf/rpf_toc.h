#pragma once

#include "core/rpf/rpf_archive.h"

namespace archon {

inline constexpr std::uint64_t rpfBlockSize = 512;
inline constexpr std::uint32_t rpfSizeInHeaderMarker = 0xFFFFFF;

bool isValidEntryName(std::string_view name);
Result<std::vector<std::byte>> serializeToc(const std::vector<RpfEntry> &entries);

}
