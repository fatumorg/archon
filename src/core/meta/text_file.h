#pragma once

#include "core/io/error.h"

#include <QString>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace archon {

inline constexpr std::uint64_t textFileLimit = 64u << 20;

struct TextFile {
    QString text;
    bool bom;
    bool crlf;
    bool latin1;
};

bool isTextMetadataName(std::string_view name);
std::string textTooLargeMessage(std::string_view name);
Result<TextFile> decodeTextFile(std::span<const std::byte> bytes, std::string_view name);
Result<std::vector<std::byte>> encodeTextFile(const TextFile &file, const QString &text);

}
