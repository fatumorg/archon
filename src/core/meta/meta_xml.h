#pragma once

#include "core/io/error.h"
#include "core/meta/meta_schema.h"
#include "core/meta/name_table.h"

#include <string>
#include <string_view>

namespace archon {

Result<std::string> metaToXml(const MetaData &data, const NameTable &names);
Result<MetaData> metaFromXml(const MetaData &original, std::string_view xml, const NameTable &names);

std::string latin1ToUtf8(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> utf8ToLatin1(std::string_view text);
std::string shortestFloat(float value);

}
