#pragma once

#include "core/io/error.h"

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

class QFile;

namespace archon {

class WritableFile {
public:
    static Result<WritableFile> open(const std::filesystem::path &file, bool create);

    WritableFile(WritableFile &&) noexcept;
    WritableFile &operator=(WritableFile &&) noexcept;
    ~WritableFile();

    std::uint64_t size() const;
    Result<std::vector<std::byte>> read(std::uint64_t offset, std::size_t count);
    Result<void> write(std::uint64_t offset, std::span<const std::byte> data);
    Result<void> resize(std::uint64_t size);
    Result<void> sync();

private:
    WritableFile();
    std::unexpected<Error> failure(std::uint64_t offset, const char *action) const;

    std::unique_ptr<QFile> file_;
    std::string name_;
};

}
