#pragma once

#include "core/io/error.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace archon {

class ZipArchive {
public:
    static Result<ZipArchive> open(std::vector<std::byte> bytes);

    ZipArchive(ZipArchive &&) noexcept;
    ZipArchive &operator=(ZipArchive &&) noexcept;
    ~ZipArchive();

    const std::vector<std::string> &names() const;
    Result<std::vector<std::byte>> read(std::string_view name, std::uint64_t limit) const;

private:
    struct State;
    explicit ZipArchive(std::unique_ptr<State> state);

    std::unique_ptr<State> state_;
};

}
