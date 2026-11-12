#pragma once

#include "core/crypto/keys.h"
#include "core/safety/transaction.h"

namespace archon {

struct ArchiveEdit {
    enum class Kind { Put, Remove, MakeDirectory, Rename };
    Kind kind;
    std::string path;
    std::filesystem::path source;
    std::string newName;
};

Result<void> editArchive(Transaction &transaction, const std::filesystem::path &file, std::span<const std::string> nested, std::span<const ArchiveEdit> edits, const Keys *keys);
Result<void> rebuildArchive(Transaction &transaction, const std::filesystem::path &file, const Keys *keys);

}
