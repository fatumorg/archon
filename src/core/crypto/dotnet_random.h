#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace archon {

class DotNetRandom {
public:
    explicit DotNetRandom(std::int32_t seed);
    void nextBytes(std::span<std::byte> out);

private:
    std::array<std::int32_t, 56> state_ {};
    int next_ = 0;
    int nextOffset_ = 21;
};

}
