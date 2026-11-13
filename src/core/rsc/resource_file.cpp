#include "core/rsc/resource_file.h"

#include "core/crypto/inflate.h"
#include "core/rsc/rsc7.h"

#include <algorithm>
#include <bit>

namespace archon {

namespace {

constexpr std::size_t oldStreamPrefixSize = 8;

std::vector<std::byte> withoutInsertedCarriageReturns(std::span<const std::byte> data)
{
    std::vector<std::byte> result;
    result.reserve(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i] == std::byte { 0x0d } && i + 1 < data.size() && data[i + 1] == std::byte { 0x0a })
            continue;
        result.push_back(data[i]);
    }
    return result;
}

constexpr std::uint64_t pageUnit = 0x200;
constexpr std::uint64_t minimumPage = 0x2000;
constexpr std::uint32_t maxShift = 15;
constexpr std::uint32_t singleUnitBit = 27;
constexpr std::uint32_t largestUnitBit = 4;
constexpr std::uint64_t largestUnits = 256;
constexpr std::size_t headerSize = 16;
constexpr std::uint64_t resourceLimit = 1ull << 31;

}

std::uint64_t singlePageSize(std::uint64_t content)
{
    return std::max(minimumPage, std::bit_ceil(std::max<std::uint64_t>(content, 1)));
}

std::uint32_t singlePageFlags(std::uint64_t size, std::uint32_t versionNibble)
{
    const std::uint32_t version = (versionNibble & 0xF) << 28;
    if (size <= pageUnit << maxShift)
        return version | (1u << singleUnitBit) | static_cast<std::uint32_t>(std::countr_zero(size / pageUnit));
    return version | (1u << largestUnitBit) | static_cast<std::uint32_t>(std::countr_zero(size / largestUnits / pageUnit));
}

Result<std::vector<std::byte>> buildResourceFile(std::uint32_t version, std::span<const std::byte> system, std::span<const std::byte> graphics)
{
    const std::uint64_t systemSize = singlePageSize(system.size());
    const std::uint64_t graphicsSize = graphics.empty() ? 0 : singlePageSize(graphics.size());
    if (systemSize + graphicsSize > resourceLimit / 2)
        return std::unexpected(Error { "rsc7", 0, "the resource would be larger than 1 GiB" });
    std::vector<std::byte> pages(static_cast<std::size_t>(systemSize + graphicsSize));
    std::ranges::copy(system, pages.begin());
    std::ranges::copy(graphics, pages.begin() + static_cast<std::ptrdiff_t>(systemSize));
    const Rsc7Header header { version, singlePageFlags(systemSize, (version >> 4) & 0xF), graphics.empty() ? (version & 0xF) << 28 : singlePageFlags(graphicsSize, version & 0xF) };
    const auto head = buildRsc7Header(header);
    std::vector<std::byte> file(head.begin(), head.end());
    const auto body = compressRaw(pages);
    if (body.empty())
        return std::unexpected(Error { "rsc7", 0, "the resource could not be compressed" });
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

Result<std::vector<std::byte>> inflateResourceBody(std::span<const std::byte> body, std::size_t pages)
{
    auto data = inflateRaw(body, pages);
    if (data)
        return data;
    const auto repaired = withoutInsertedCarriageReturns(body);
    if (repaired.size() != body.size()) {
        if (auto recovered = inflateRaw(repaired, pages))
            return recovered;
    }
    if (body.size() > oldStreamPrefixSize) {
        if (auto recovered = inflateRaw(body.subspan(oldStreamPrefixSize), pages))
            return recovered;
    }
    return data;
}

Result<std::vector<std::byte>> inflateResource(std::span<const std::byte> file)
{
    if (file.size() < headerSize)
        return std::unexpected(Error { "rsc7", 0, "resource is smaller than its header" });
    const auto header = parseRsc7Header(file.first(headerSize));
    if (!header)
        return std::unexpected(header.error());
    const std::uint64_t size = rsc7SizeFromFlags(header->systemFlags) + rsc7SizeFromFlags(header->graphicsFlags);
    if (size > resourceLimit)
        return std::unexpected(Error { "rsc7", 0, "declared size exceeds the allocation limit" });
    return inflateResourceBody(file.subspan(headerSize), static_cast<std::size_t>(size));
}

}
