#pragma once

#include "core/io/error.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace archon {

class ZipArchive {
public:
    static Result<ZipArchive> open(std::vector<std::byte> bytes);
    static Result<ZipArchive> open(const std::filesystem::path &file);

    ZipArchive(ZipArchive &&) noexcept;
    ZipArchive &operator=(ZipArchive &&) noexcept;
    ~ZipArchive();

    const std::vector<std::string> &names() const;
    Result<std::vector<std::byte>> read(std::string_view name, std::uint64_t limit) const;
    Result<void> extract(std::string_view name, const std::filesystem::path &destination) const;

private:
    struct State;
    explicit ZipArchive(std::unique_ptr<State> state);
    static ZipArchive listed(std::unique_ptr<State> state);
    Result<std::pair<std::uint64_t, std::uint64_t>> member(const std::string &name) const;

    std::unique_ptr<State> state_;
};

}
