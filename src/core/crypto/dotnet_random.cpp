#include "core/crypto/dotnet_random.h"

#include <cstdlib>

namespace archon {

namespace {

constexpr std::int32_t big = 2147483647;

}

DotNetRandom::DotNetRandom(std::int32_t seed)
{
    const std::int32_t magnitude = seed == INT32_MIN ? big : std::abs(seed);
    std::int32_t mj = 161803398 - magnitude;
    state_[55] = mj;
    std::int32_t mk = 1;
    for (int i = 1; i < 55; ++i) {
        const int slot = (21 * i) % 55;
        state_[slot] = mk;
        mk = mj - mk;
        if (mk < 0)
            mk += big;
        mj = state_[slot];
    }
    for (int round = 0; round < 4; ++round) {
        for (int i = 1; i < 56; ++i) {
            state_[i] = static_cast<std::int32_t>(static_cast<std::uint32_t>(state_[i]) - static_cast<std::uint32_t>(state_[1 + (i + 30) % 55]));
            if (state_[i] < 0)
                state_[i] += big;
        }
    }
    next_ = 0;
    nextOffset_ = 21;
}

void DotNetRandom::nextBytes(std::span<std::byte> out)
{
    for (std::byte &target : out) {
        if (++next_ >= 56)
            next_ = 1;
        if (++nextOffset_ >= 56)
            nextOffset_ = 1;
        std::int32_t value = state_[next_] - state_[nextOffset_];
        if (value == big)
            --value;
        if (value < 0)
            value += big;
        state_[next_] = value;
        target = static_cast<std::byte>(value % 256);
    }
}

}
