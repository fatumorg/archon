#pragma once

#include "core/rpf/rpf_archive.h"

#include <filesystem>
#include <memory>

namespace archon {

class RandomAccessSource {
public:
    virtual ~RandomAccessSource() = default;
    virtual std::uint64_t size() const = 0;
    virtual Result<std::vector<std::byte>> read(std::uint64_t offset, std::size_t count) const = 0;
};

std::shared_ptr<RandomAccessSource> openFileSource(const std::filesystem::path &file);
std::shared_ptr<RandomAccessSource> openSubSource(std::shared_ptr<RandomAccessSource> parent, std::uint64_t offset, std::uint64_t size);

inline constexpr int maxArchiveDepth = 8;

class RpfReader {
public:
    static Result<RpfReader> open(std::shared_ptr<RandomAccessSource> source, std::string_view archiveName, const Keys *keys, int depth = 0);

    const RpfArchive &archive() const;
    Result<std::vector<std::byte>> fileData(std::uint32_t entryIndex) const;
    Result<std::vector<std::byte>> storedFile(std::uint32_t entryIndex) const;
    Result<RpfReader> openNested(std::uint32_t entryIndex) const;

private:
    Result<const RpfEntry *> fileEntry(std::uint32_t entryIndex) const;
    Result<std::vector<std::byte>> decrypted(std::vector<std::byte> data, const RpfEntry &entry, std::uint32_t length) const;
    Result<std::vector<std::byte>> binaryData(const RpfEntry &entry) const;
    Result<std::vector<std::byte>> resourceBody(const RpfEntry &entry) const;

    std::shared_ptr<RandomAccessSource> source_;
    RpfArchive archive_;
    const Keys *keys_ = nullptr;
    int depth_ = 0;
};

}
