#pragma once

#include <array>
#include <cstddef>

namespace archon {

using Digest = std::array<std::byte, 20>;

extern const Digest aesKeyDigest;
extern const Digest hashLookupDigest;
extern const std::array<Digest, 101> ngKeyDigests;
extern const std::array<Digest, 272> ngTableDigests;

}
