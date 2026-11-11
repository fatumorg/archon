#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace archon {

constexpr char asciiLower(char character)
{
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

inline std::string asciiLower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char character) { return asciiLower(character); });
    return result;
}

inline bool asciiEqualIgnoreCase(std::string_view left, std::string_view right)
{
    return std::ranges::equal(left, right, [](char a, char b) { return asciiLower(a) == asciiLower(b); });
}

}
