#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace archon {

inline constexpr std::uint64_t resourceSystemBase = 0x50000000;
inline constexpr std::uint64_t resourceGraphicsBase = 0x60000000;

class ResourcePages {
public:
    ResourcePages(std::span<const std::byte> pages, std::uint64_t systemSize)
        : pages_(pages)
        , systemSize_(std::min<std::uint64_t>(systemSize, pages.size()))
    {
    }

    std::uint64_t value(std::uint64_t offset, int size) const
    {
        std::uint64_t result = 0;
        for (int i = 0; i < size; ++i)
            result |= std::to_integer<std::uint64_t>(pages_[offset + i]) << (8 * i);
        return result;
    }

    std::optional<std::uint64_t> system(std::uint64_t pointer, std::uint64_t length) const
    {
        if (pointer < resourceSystemBase || pointer - resourceSystemBase > systemSize_ || length > systemSize_ - (pointer - resourceSystemBase))
            return std::nullopt;
        return pointer - resourceSystemBase;
    }

    std::optional<std::uint64_t> graphics(std::uint64_t pointer, std::uint64_t length) const
    {
        const std::uint64_t size = pages_.size() - systemSize_;
        if (pointer < resourceGraphicsBase || pointer - resourceGraphicsBase > size || length > size - (pointer - resourceGraphicsBase))
            return std::nullopt;
        return systemSize_ + (pointer - resourceGraphicsBase);
    }

    std::optional<std::uint64_t> resolve(std::uint64_t pointer, std::uint64_t length) const
    {
        return pointer >= resourceGraphicsBase ? graphics(pointer, length) : system(pointer, length);
    }

    std::optional<std::string> string(std::uint64_t pointer, std::size_t maxLength) const
    {
        const auto start = system(pointer, 1);
        if (!start)
            return std::nullopt;
        std::string text;
        for (std::uint64_t offset = *start; offset < systemSize_ && text.size() <= maxLength; ++offset) {
            const char c = static_cast<char>(pages_[offset]);
            if (c == '\0')
                return text;
            text += c;
        }
        return std::nullopt;
    }

    std::span<const std::byte> bytes(std::uint64_t offset, std::uint64_t length) const
    {
        return pages_.subspan(offset, length);
    }

    std::uint64_t systemSize() const
    {
        return systemSize_;
    }

private:
    std::span<const std::byte> pages_;
    std::uint64_t systemSize_;
};

}
