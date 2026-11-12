#include "core/ytd/texture_dictionary.h"

#include "core/crypto/hash.h"
#include "core/io/ascii.h"
#include "core/rsc/resource_file.h"
#include "core/ytd/texture_format.h"

#include <algorithm>
#include <array>
#include <bit>

namespace archon {

namespace {

constexpr char format[] = "ytd";
constexpr std::uint64_t systemBase = 0x50000000;
constexpr std::uint64_t graphicsBase = 0x60000000;
constexpr std::uint32_t dictionaryVft = 0x40571500;
constexpr std::uint32_t textureVft = 0x40617568;
constexpr std::uint32_t enhancedDictionaryVft = 0x4068c308;
constexpr std::uint32_t enhancedTextureVft = 0x406b7940;
constexpr std::uint32_t enhancedViewVft = 0x406b77d8;
constexpr std::size_t dictionarySize = 0x40;
constexpr std::size_t textureSize = 0x90;
constexpr std::size_t enhancedTextureSize = 0x58;
constexpr std::size_t enhancedViewSize = 0x28;
constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 12> dxgiFormats { {
    { 71, 0x31545844 }, { 74, 0x33545844 }, { 77, 0x35545844 }, { 80, 0x31495441 }, { 83, 0x32495441 }, { 98, 0x20374342 },
    { 87, 21 }, { 28, 32 }, { 85, 23 }, { 86, 25 }, { 65, 28 }, { 61, 50 },
} };

std::uint32_t formatFromDxgi(std::uint32_t dxgi)
{
    const auto found = std::ranges::find(dxgiFormats, dxgi, &std::pair<std::uint32_t, std::uint32_t>::first);
    return found == dxgiFormats.end() ? 0x80000000u | dxgi : found->second;
}

std::optional<std::uint32_t> dxgiFromFormat(std::uint32_t format)
{
    const auto found = std::ranges::find(dxgiFormats, format, &std::pair<std::uint32_t, std::uint32_t>::second);
    return found == dxgiFormats.end() ? std::nullopt : std::optional(found->first);
}

std::uint32_t usageFor(std::uint32_t usage, std::uint32_t version)
{
    const bool enhancedUsage = (usage & 0xFFFFFF00u) == 0x00800200u;
    if (version == enhancedDictionaryVersion)
        return enhancedUsage ? usage : 0x00800200u | (usage & 0xFFu);
    return enhancedUsage ? 0x20000000u | (usage & 0xFFu) : usage;
}
constexpr std::size_t pagesInfoSize = 0x20;
constexpr std::size_t maxNameLength = 255;
constexpr std::uint32_t maxLevels = 16;
constexpr std::uint64_t dataAlignment = 0x200;

std::unexpected<Error> failure(std::string message, std::uint64_t offset = 0)
{
    return std::unexpected(Error { format, offset, std::move(message) });
}

class Pages {
public:
    Pages(std::span<const std::byte> pages, std::uint64_t systemSize)
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
        if (pointer < systemBase || pointer - systemBase > systemSize_ || length > systemSize_ - (pointer - systemBase))
            return std::nullopt;
        return pointer - systemBase;
    }

    std::optional<std::uint64_t> graphics(std::uint64_t pointer, std::uint64_t length) const
    {
        const std::uint64_t size = pages_.size() - systemSize_;
        if (pointer < graphicsBase || pointer - graphicsBase > size || length > size - (pointer - graphicsBase))
            return std::nullopt;
        return systemSize_ + (pointer - graphicsBase);
    }

    std::optional<std::string> string(std::uint64_t pointer) const
    {
        const auto start = system(pointer, 1);
        if (!start)
            return std::nullopt;
        std::string text;
        for (std::uint64_t offset = *start; offset < systemSize_ && text.size() <= maxNameLength; ++offset) {
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

private:
    std::span<const std::byte> pages_;
    std::uint64_t systemSize_;
};

void put(std::vector<std::byte> &bytes, std::size_t offset, std::uint64_t value, int size)
{
    for (int i = 0; i < size; ++i)
        bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}

std::size_t aligned(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

}

std::uint32_t textureNameHash(std::string_view name)
{
    const std::string lower = asciiLower(name);
    return jenkinsHash(std::as_bytes(std::span(lower.data(), lower.size())));
}

Result<std::vector<Texture>> readTextureDictionary(std::span<const std::byte> bytes, std::uint64_t systemSize, std::uint32_t version)
{
    if (version != legacyDictionaryVersion && version != enhancedDictionaryVersion)
        return failure("texture dictionary version " + std::to_string(version) + " is not supported");
    const bool enhanced = version == enhancedDictionaryVersion;
    const Pages pages(bytes, systemSize);
    if (systemSize < dictionarySize || bytes.size() < dictionarySize)
        return failure("the texture dictionary is truncated");
    const std::uint64_t count = pages.value(0x38, 2);
    if (count == 0)
        return std::vector<Texture>();
    const auto list = pages.system(pages.value(0x30, 8), count * 8);
    if (!list)
        return failure("the texture list lies outside the resource", 0x30);
    std::vector<Texture> textures;
    textures.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        const auto at = pages.system(pages.value(*list + index * 8, 8), enhanced ? enhancedTextureSize : textureSize);
        if (!at)
            return failure("a texture lies outside the resource", *list + index * 8);
        const std::uint64_t t = *at;
        const auto name = pages.string(pages.value(t + 0x28, 8));
        if (!name || name->empty())
            return failure("a texture name lies outside the resource", t + 0x28);
        Texture texture = enhanced
            ? Texture { *name, static_cast<std::uint32_t>(pages.value(t + 0x18, 2)), static_cast<std::uint32_t>(pages.value(t + 0x1A, 2)), static_cast<std::uint32_t>(pages.value(t + 0x22, 1)), formatFromDxgi(static_cast<std::uint32_t>(pages.value(t + 0x1F, 1))), static_cast<std::uint32_t>(pages.value(t + 0x40, 4)), {} }
            : Texture { *name, static_cast<std::uint32_t>(pages.value(t + 0x50, 2)), static_cast<std::uint32_t>(pages.value(t + 0x52, 2)), static_cast<std::uint32_t>(pages.value(t + 0x5D, 1)), static_cast<std::uint32_t>(pages.value(t + 0x58, 4)), static_cast<std::uint32_t>(pages.value(t + 0x40, 4)), {} };
        if (texture.width == 0 || texture.height == 0 || texture.levels == 0 || texture.levels > maxLevels)
            return failure(texture.name + " has an invalid size or mip count", t);
        if (const auto size = mipChainSize(texture.format, texture.width, texture.height, texture.levels)) {
            const auto data = pages.graphics(pages.value(t + (enhanced ? 0x38 : 0x70), 8), *size);
            if (!data)
                return failure(texture.name + " has pixel data outside the resource", t);
            const auto span = pages.bytes(*data, *size);
            texture.data.assign(span.begin(), span.end());
        }
        textures.push_back(std::move(texture));
    }
    return textures;
}

Result<std::vector<std::byte>> writeTextureDictionary(std::span<const Texture> input, std::uint32_t version)
{
    if (version != legacyDictionaryVersion && version != enhancedDictionaryVersion)
        return failure("texture dictionary version " + std::to_string(version) + " is not supported");
    const bool enhanced = version == enhancedDictionaryVersion;
    std::vector<const Texture *> textures;
    for (const Texture &texture : input) {
        const auto size = mipChainSize(texture.format, texture.width, texture.height, texture.levels);
        if (!size)
            return failure(texture.name + " uses texture format " + std::to_string(texture.format) + ", which ARCHON cannot rewrite.");
        if (texture.data.size() != *size)
            return failure(texture.name + " has " + std::to_string(texture.data.size()) + " bytes of pixel data, expected " + std::to_string(*size));
        if (texture.name.empty() || texture.name.size() > maxNameLength)
            return failure("a texture name is empty or too long");
        if (texture.width == 0 || texture.height == 0 || texture.width > 0xFFFF || texture.height > 0xFFFF || textureStride(texture.format, texture.width) > 0xFFFF)
            return failure(texture.name + " is larger than 65535 pixels or its row is too long to store");
        if (texture.levels == 0 || texture.levels > static_cast<std::uint32_t>(std::bit_width(std::max(texture.width, texture.height))))
            return failure(texture.name + " has more mip levels than its size allows");
        if (enhanced && !dxgiFromFormat(texture.format))
            return failure(texture.name + " uses texture format " + std::to_string(texture.format) + ", which ARCHON cannot rewrite.");
        textures.push_back(&texture);
    }
    std::ranges::sort(textures, {}, [](const Texture *texture) { return textureNameHash(texture->name); });

    const std::size_t count = textures.size();
    const std::size_t texturesAt = dictionarySize;
    const std::size_t structSize = enhanced ? enhancedTextureSize + enhancedViewSize : textureSize;
    const std::size_t pagesInfoAt = aligned(texturesAt + count * structSize, 16);
    const std::size_t hashesAt = pagesInfoAt + pagesInfoSize;
    const std::size_t pointersAt = aligned(hashesAt + count * 4, 16);
    std::size_t namesEnd = aligned(pointersAt + count * 8, 16);
    std::vector<std::size_t> nameAt;
    for (const Texture *texture : textures) {
        nameAt.push_back(namesEnd);
        namesEnd = aligned(namesEnd + texture->name.size() + 1, 16);
    }
    std::vector<std::size_t> dataAt;
    std::size_t graphicsEnd = 0;
    for (const Texture *texture : textures) {
        dataAt.push_back(graphicsEnd);
        graphicsEnd = aligned(graphicsEnd + texture->data.size(), enhanced ? 16 : dataAlignment);
    }

    std::vector<std::byte> system(namesEnd);
    std::vector<std::byte> graphics(graphicsEnd);
    put(system, 0x00, enhanced ? enhancedDictionaryVft : dictionaryVft, 4);
    put(system, 0x04, 1, 4);
    put(system, 0x08, systemBase + pagesInfoAt, 8);
    put(system, 0x18, 1, 4);
    put(system, 0x20, systemBase + hashesAt, 8);
    put(system, 0x28, count, 2);
    put(system, 0x2A, count, 2);
    put(system, 0x30, systemBase + pointersAt, 8);
    put(system, 0x38, count, 2);
    put(system, 0x3A, count, 2);
    put(system, pagesInfoAt + 8, 1, 1);
    put(system, pagesInfoAt + 9, count == 0 ? 0 : 1, 1);
    for (std::size_t index = 0; index < count; ++index) {
        const Texture &texture = *textures[index];
        const std::size_t t = texturesAt + index * structSize;
        put(system, hashesAt + index * 4, textureNameHash(texture.name), 4);
        put(system, pointersAt + index * 8, systemBase + t, 8);
        for (std::size_t c = 0; c < texture.name.size(); ++c)
            system[nameAt[index] + c] = static_cast<std::byte>(texture.name[c]);
        std::ranges::copy(texture.data, graphics.begin() + static_cast<std::ptrdiff_t>(dataAt[index]));
        if (enhanced) {
            const auto info = textureFormat(texture.format);
            const std::uint32_t element = info->block ? info->bitsPerPixel * 2 : info->bitsPerPixel / 8;
            const std::size_t view = t + enhancedTextureSize;
            put(system, t + 0x00, enhancedTextureVft, 4);
            put(system, t + 0x04, 1, 4);
            put(system, t + 0x08, texture.data.size() / element, 4);
            put(system, t + 0x0C, element, 4);
            put(system, t + 0x10, 0x00260208, 4);
            put(system, t + 0x18, texture.width, 2);
            put(system, t + 0x1A, texture.height, 2);
            put(system, t + 0x1C, 1, 2);
            put(system, t + 0x1E, 1, 1);
            put(system, t + 0x1F, *dxgiFromFormat(texture.format), 1);
            put(system, t + 0x20, 0xFF, 1);
            put(system, t + 0x22, texture.levels, 1);
            put(system, t + 0x23, 0x28, 1);
            put(system, t + 0x24, 0x00010000, 4);
            put(system, t + 0x28, systemBase + nameAt[index], 8);
            put(system, t + 0x30, systemBase + view, 8);
            put(system, t + 0x38, graphicsBase + dataAt[index], 8);
            put(system, t + 0x40, usageFor(texture.usage, version), 4);
            put(system, t + 0x44, 2, 4);
            put(system, view + 0x00, enhancedViewVft, 4);
            put(system, view + 0x04, 1, 4);
            put(system, view + 0x10, 0xFFFF0041, 4);
            put(system, view + 0x14, 0xFFFFFFFF, 4);
            continue;
        }
        put(system, t + 0x00, textureVft, 4);
        put(system, t + 0x04, 1, 4);
        put(system, t + 0x28, systemBase + nameAt[index], 8);
        put(system, t + 0x30, 1, 2);
        put(system, t + 0x32, 0x80, 2);
        put(system, t + 0x40, usageFor(texture.usage, version), 4);
        put(system, t + 0x50, texture.width, 2);
        put(system, t + 0x52, texture.height, 2);
        put(system, t + 0x54, 1, 2);
        put(system, t + 0x56, textureStride(texture.format, texture.width), 2);
        put(system, t + 0x58, texture.format, 4);
        put(system, t + 0x5D, texture.levels, 1);
        put(system, t + 0x70, graphicsBase + dataAt[index], 8);
    }
    return buildResourceFile(version, system, graphics);
}

}
