#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace archon {

inline void putInteger(std::vector<std::byte> &out, std::uint64_t value, int width)
{
    for (int i = 0; i < width; ++i)
        out.push_back(static_cast<std::byte>(value >> (8 * i)));
}

inline void putBytes(std::vector<std::byte> &out, std::span<const std::byte> bytes)
{
    out.insert(out.end(), bytes.begin(), bytes.end());
}

inline void putString(std::vector<std::byte> &out, std::string_view text)
{
    putInteger(out, text.size(), 4);
    putBytes(out, std::as_bytes(std::span(text.data(), text.size())));
}

}
