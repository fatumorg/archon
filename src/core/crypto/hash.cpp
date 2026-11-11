#include "core/crypto/hash.h"

#include <openssl/evp.h>

namespace archon {

std::uint32_t jenkinsHash(std::span<const std::byte> data)
{
    std::uint32_t hash = 0;
    for (const std::byte value : data) {
        hash += std::to_integer<std::uint32_t>(value);
        hash += hash << 10;
        hash ^= hash >> 6;
    }
    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;
    return hash;
}

std::array<std::byte, 20> sha1(std::span<const std::byte> data)
{
    std::array<std::byte, 20> digest {};
    EVP_Digest(data.data(), data.size(), reinterpret_cast<unsigned char *>(digest.data()), nullptr, EVP_sha1(), nullptr);
    return digest;
}

std::array<std::byte, 32> sha256(std::span<const std::byte> data)
{
    std::array<std::byte, 32> digest {};
    EVP_Digest(data.data(), data.size(), reinterpret_cast<unsigned char *>(digest.data()), nullptr, EVP_sha256(), nullptr);
    return digest;
}

}
