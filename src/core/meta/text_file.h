#pragma once

#include "core/io/error.h"

#include <QString>

#include <span>
#include <string_view>
#include <vector>

namespace archon {

struct TextFile {
    QString text;
    bool bom;
    bool crlf;
    bool latin1;
};

bool isTextMetadataName(std::string_view name);
Result<TextFile> decodeTextFile(std::span<const std::byte> bytes, std::string_view name);
std::vector<std::byte> encodeTextFile(const TextFile &file, const QString &text);

}
