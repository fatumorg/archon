#pragma once

#include "core/crypto/key_digests.h"
#include "core/io/error.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>

namespace archon {

struct Keys {
    std::array<std::byte, 32> aes;
    std::array<std::array<std::uint32_t, 68>, 101> ngKeys;
    std::array<std::array<std::array<std::uint32_t, 256>, 16>, 17> ngTables;
    std::array<std::uint8_t, 256> hashLookup;
};

Result<std::array<std::byte, 32>> findAesKey(std::span<const std::byte> executable);
Result<std::array<std::byte, 32>> findAesKey(std::span<const std::byte> executable, const Digest &digest);
Result<Keys> unwrapKeys(std::span<const std::byte, 32> aesKey, std::span<const std::byte> magic);
Result<Keys> loadKeys(const std::filesystem::path &executable, const std::filesystem::path &magic, const std::filesystem::path &cacheFile);

}
