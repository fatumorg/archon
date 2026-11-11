#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace archon {

struct Error {
    std::string format;
    std::uint64_t offset;
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

inline constexpr std::size_t maxAllocation = 1u << 30;

}
