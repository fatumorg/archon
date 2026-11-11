#pragma once

#include "core/crypto/keys.h"
#include "core/io/error.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace archon {

enum class RpfEncryption { None, Open, Aes, Ng };
enum class RpfEntryKind { Directory, Binary, Resource };

struct RpfEntry {
    RpfEntryKind kind;
    std::string name;
    std::uint32_t parent;
    std::uint32_t firstChild;
    std::uint32_t childCount;
    std::uint64_t offset;
    std::uint32_t storedSize;
    std::uint32_t uncompressedSize;
    bool encrypted;
    std::uint32_t systemFlags;
    std::uint32_t graphicsFlags;
};

class RpfArchive {
public:
    static Result<RpfArchive> parse(std::span<const std::byte> headerAndToc, std::string_view archiveName, std::uint64_t archiveSize, const Keys *keys);
    static Result<std::size_t> tocSize(std::span<const std::byte> first16Bytes);

    RpfEncryption encryption() const;
    const std::vector<RpfEntry> &entries() const;
    std::optional<std::uint32_t> find(std::string_view path) const;
    std::string path(std::uint32_t index) const;

private:
    RpfEncryption encryption_ = RpfEncryption::None;
    std::vector<RpfEntry> entries_;
};

}
