#include "core/crypto/ng.h"

namespace archon {

namespace {

using Block = std::array<std::uint8_t, 16>;
using RoundTables = std::array<std::array<std::uint32_t, 256>, 16>;

void store(Block &block, const std::array<std::uint32_t, 4> &words)
{
    for (std::size_t i = 0; i < 4; ++i) {
        block[4 * i] = static_cast<std::uint8_t>(words[i]);
        block[4 * i + 1] = static_cast<std::uint8_t>(words[i] >> 8);
        block[4 * i + 2] = static_cast<std::uint8_t>(words[i] >> 16);
        block[4 * i + 3] = static_cast<std::uint8_t>(words[i] >> 24);
    }
}

void roundA(Block &d, std::span<const std::uint32_t, 4> k, const RoundTables &t)
{
    std::array<std::uint32_t, 4> x {};
    for (std::size_t i = 0; i < 4; ++i)
        x[i] = t[4 * i][d[4 * i]] ^ t[4 * i + 1][d[4 * i + 1]] ^ t[4 * i + 2][d[4 * i + 2]] ^ t[4 * i + 3][d[4 * i + 3]] ^ k[i];
    store(d, x);
}

void roundB(Block &d, std::span<const std::uint32_t, 4> k, const RoundTables &t)
{
    const std::array<std::uint32_t, 4> x {
        t[0][d[0]] ^ t[7][d[7]] ^ t[10][d[10]] ^ t[13][d[13]] ^ k[0],
        t[1][d[1]] ^ t[4][d[4]] ^ t[11][d[11]] ^ t[14][d[14]] ^ k[1],
        t[2][d[2]] ^ t[5][d[5]] ^ t[8][d[8]] ^ t[15][d[15]] ^ k[2],
        t[3][d[3]] ^ t[6][d[6]] ^ t[9][d[9]] ^ t[12][d[12]] ^ k[3],
    };
    store(d, x);
}

}

std::uint32_t ngHash(const Keys &keys, std::string_view lowerCaseName)
{
    std::uint32_t result = 0;
    for (const char character : lowerCaseName) {
        const std::uint32_t mixed = 1025u * (keys.hashLookup[static_cast<std::uint8_t>(character)] + result);
        result = (mixed >> 6) ^ mixed;
    }
    const std::uint32_t folded = 9u * result;
    return 32769u * ((folded >> 11) ^ folded);
}

std::vector<std::byte> ngDecrypt(const Keys &keys, std::span<const std::byte> data, std::string_view name, std::uint32_t length)
{
    const std::span<const std::uint32_t, 68> key(keys.ngKeys[(ngHash(keys, name) + length + 61u) % 101u]);
    std::vector<std::byte> result(data.begin(), data.end());
    for (std::size_t offset = 0; result.size() - offset >= 16; offset += 16) {
        Block block {};
        for (std::size_t i = 0; i < 16; ++i)
            block[i] = std::to_integer<std::uint8_t>(result[offset + i]);
        roundA(block, key.subspan<0, 4>(), keys.ngTables[0]);
        roundA(block, key.subspan<4, 4>(), keys.ngTables[1]);
        for (std::size_t round = 2; round <= 15; ++round)
            roundB(block, std::span<const std::uint32_t, 4>(key.subspan(4 * round, 4)), keys.ngTables[round]);
        roundA(block, key.subspan<64, 4>(), keys.ngTables[16]);
        for (std::size_t i = 0; i < 16; ++i)
            result[offset + i] = static_cast<std::byte>(block[i]);
    }
    return result;
}

}
