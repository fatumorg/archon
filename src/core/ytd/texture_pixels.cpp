#include "core/ytd/texture_pixels.h"

#include "core/ytd/texture_format.h"

#define BCDEC_IMPLEMENTATION
#include "third_party/bcdec.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace archon {

namespace {

constexpr char format[] = "dds";
constexpr std::uint32_t ddsMagic = 0x20534444;
constexpr std::uint32_t dx10 = 0x30315844;
constexpr std::size_t headerEnd = 4 + 124;
constexpr std::size_t dx10End = headerEnd + 20;
constexpr std::uint32_t fourCcFlag = 0x4;
constexpr std::uint32_t alphaPixels = 0x1;
constexpr std::uint32_t alphaOnly = 0x2;
constexpr std::uint32_t rgbFlag = 0x40;
constexpr std::uint32_t luminance = 0x20000;
constexpr std::uint32_t maxSide = 16384;
constexpr std::uint64_t dataLimit = 1ull << 31;

constexpr std::uint32_t dxt1 = 0x31545844;
constexpr std::uint32_t dxt3 = 0x33545844;
constexpr std::uint32_t dxt5 = 0x35545844;
constexpr std::uint32_t ati1 = 0x31495441;
constexpr std::uint32_t ati2 = 0x32495441;
constexpr std::uint32_t bc7 = 0x20374342;

struct Masks {
    std::uint32_t format;
    std::uint32_t flags;
    std::uint32_t bits;
    std::array<std::uint32_t, 4> masks;
};

constexpr std::array<Masks, 7> uncompressed { {
    { 21, rgbFlag | alphaPixels, 32, { 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 } },
    { 22, rgbFlag, 32, { 0x00FF0000, 0x0000FF00, 0x000000FF, 0 } },
    { 32, rgbFlag | alphaPixels, 32, { 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000 } },
    { 23, rgbFlag, 16, { 0xF800, 0x07E0, 0x001F, 0 } },
    { 25, rgbFlag | alphaPixels, 16, { 0x7C00, 0x03E0, 0x001F, 0x8000 } },
    { 28, alphaOnly, 8, { 0, 0, 0, 0xFF } },
    { 50, luminance, 8, { 0xFF, 0, 0, 0 } },
} };

constexpr std::uint32_t enhancedOnly = 0x80000000;
constexpr std::uint32_t r16 = enhancedOnly | 56;

constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 6> dxgiFormats { {
    { 71, dxt1 }, { 74, dxt3 }, { 77, dxt5 }, { 80, ati1 }, { 83, ati2 }, { 98, bc7 },
} };

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { format, 0, std::move(message) });
}

std::uint32_t word(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return value;
}

void putWord(std::vector<std::byte> &bytes, std::size_t offset, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}

std::uint8_t expand(std::uint32_t value, std::uint32_t mask)
{
    if (mask == 0)
        return 0;
    const int shift = std::countr_zero(mask);
    const std::uint32_t maximum = mask >> shift;
    return static_cast<std::uint8_t>((((value & mask) >> shift) * 255 + maximum / 2) / maximum);
}

}

Result<std::vector<std::byte>> decodeRgba(const Texture &texture, std::uint32_t level)
{
    const auto info = textureFormat(texture.format);
    if (!info)
        return failure("texture format " + std::to_string(texture.format) + " cannot be shown");
    if (level >= texture.levels)
        return failure("the texture has no mip level " + std::to_string(level));
    const std::uint32_t width = std::max(1u, texture.width >> level);
    const std::uint32_t height = std::max(1u, texture.height >> level);
    const auto offset = mipOffset(texture.format, texture.width, texture.height, level);
    const auto end = mipOffset(texture.format, texture.width, texture.height, level + 1);
    if (!offset || !end || *end > texture.data.size())
        return failure("the texture data is shorter than its mip chain");
    const std::span<const std::byte> source = std::span(texture.data).subspan(static_cast<std::size_t>(*offset), static_cast<std::size_t>(*end - *offset));
    std::vector<std::byte> rgba(std::size_t(width) * height * 4);

    if (info->block) {
        const std::uint32_t blocksWide = std::max(1u, (width + 3) / 4);
        const std::uint32_t blocksHigh = std::max(1u, (height + 3) / 4);
        const std::size_t blockBytes = info->bitsPerPixel * 2;
        std::array<std::uint8_t, 64> block {};
        for (std::uint32_t by = 0; by < blocksHigh; ++by) {
            for (std::uint32_t bx = 0; bx < blocksWide; ++bx) {
                const void *compressed = source.data() + (std::size_t(by) * blocksWide + bx) * blockBytes;
                block.fill(0);
                switch (withoutSrgb(texture.format)) {
                case dxt1:
                    bcdec_bc1(compressed, block.data(), 16);
                    break;
                case dxt3:
                    bcdec_bc2(compressed, block.data(), 16);
                    break;
                case dxt5:
                    bcdec_bc3(compressed, block.data(), 16);
                    break;
                case bc7:
                    bcdec_bc7(compressed, block.data(), 16);
                    break;
                case ati1: {
                    std::array<std::uint8_t, 16> channel {};
                    bcdec_bc4(compressed, channel.data(), 4);
                    for (int i = 0; i < 16; ++i)
                        block[i * 4] = block[i * 4 + 1] = block[i * 4 + 2] = channel[i], block[i * 4 + 3] = 255;
                    break;
                }
                case ati2: {
                    std::array<std::uint8_t, 32> channels {};
                    bcdec_bc5(compressed, channels.data(), 8);
                    for (int i = 0; i < 16; ++i) {
                        const double x = channels[i * 2] / 127.5 - 1.0;
                        const double y = channels[i * 2 + 1] / 127.5 - 1.0;
                        const double z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
                        block[i * 4] = channels[i * 2];
                        block[i * 4 + 1] = channels[i * 2 + 1];
                        block[i * 4 + 2] = static_cast<std::uint8_t>(std::lround((z + 1.0) * 127.5));
                        block[i * 4 + 3] = 255;
                    }
                    break;
                }
                default:
                    break;
                }
                for (std::uint32_t y = 0; y < 4 && by * 4 + y < height; ++y) {
                    for (std::uint32_t x = 0; x < 4 && bx * 4 + x < width; ++x) {
                        const std::size_t target = ((std::size_t(by) * 4 + y) * width + bx * 4 + x) * 4;
                        for (int c = 0; c < 4; ++c)
                            rgba[target + c] = static_cast<std::byte>(block[(y * 4 + x) * 4 + c]);
                    }
                }
            }
        }
        return rgba;
    }

    const auto masks = std::ranges::find(uncompressed, texture.format, &Masks::format);
    const std::size_t pixelBytes = info->bitsPerPixel / 8;
    for (std::size_t index = 0; index < std::size_t(width) * height; ++index) {
        std::uint32_t value = 0;
        for (std::size_t b = 0; b < pixelBytes; ++b)
            value |= std::to_integer<std::uint32_t>(source[index * pixelBytes + b]) << (8 * b);
        std::array<std::uint8_t, 4> out {};
        if (texture.format == r16) {
            const auto level = static_cast<std::uint8_t>(value >> 8);
            out = { level, level, level, 255 };
        } else if (texture.format == 50) {
            out = { static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value), 255 };
        } else if (texture.format == 28) {
            out = { 255, 255, 255, static_cast<std::uint8_t>(value) };
        } else {
            for (int c = 0; c < 4; ++c)
                out[c] = expand(value, masks->masks[c]);
            if (masks->masks[3] == 0)
                out[3] = 255;
        }
        for (int c = 0; c < 4; ++c)
            rgba[index * 4 + c] = static_cast<std::byte>(out[c]);
    }
    return rgba;
}

Result<std::vector<std::byte>> writeDds(const Texture &texture)
{
    const auto info = textureFormat(texture.format);
    const auto size = mipChainSize(texture.format, texture.width, texture.height, texture.levels);
    if (!info || !size || texture.data.size() != *size)
        return failure(texture.name + " uses texture format " + std::to_string(texture.format) + ", which cannot be exported");
    const bool extended = texture.format == bc7 || (texture.format & enhancedOnly) != 0;
    std::vector<std::byte> dds(extended ? dx10End : headerEnd);
    putWord(dds, 0, ddsMagic);
    putWord(dds, 4, 124);
    putWord(dds, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | (info && info->block ? 0x80000 : 0x8));
    putWord(dds, 12, texture.height);
    putWord(dds, 16, texture.width);
    if (info && info->block)
        putWord(dds, 20, static_cast<std::uint32_t>(*mipOffset(texture.format, texture.width, texture.height, 1)));
    else
        putWord(dds, 20, textureStride(texture.format, texture.width));
    putWord(dds, 28, texture.levels);
    putWord(dds, 76, 32);
    if (extended) {
        putWord(dds, 80, fourCcFlag);
        putWord(dds, 84, dx10);
    } else if (info && info->block) {
        putWord(dds, 80, fourCcFlag);
        putWord(dds, 84, texture.format);
    } else if (const auto masks = std::ranges::find(uncompressed, texture.format, &Masks::format); masks != uncompressed.end()) {
        putWord(dds, 80, masks->flags);
        putWord(dds, 88, masks->bits);
        for (int c = 0; c < 4; ++c)
            putWord(dds, 92 + c * 4, masks->masks[c]);
    }
    putWord(dds, 108, 0x1000 | (texture.levels > 1 ? 0x400008 : 0));
    if (extended) {
        putWord(dds, 128, texture.format == bc7 ? 98 : texture.format & 0xFFu);
        putWord(dds, 132, 3);
        putWord(dds, 140, 1);
    }
    dds.insert(dds.end(), texture.data.begin(), texture.data.end());
    return dds;
}

Result<Texture> readDds(std::span<const std::byte> bytes, std::string name)
{
    if (bytes.size() < headerEnd || word(bytes, 0) != ddsMagic || word(bytes, 4) != 124)
        return failure("not a DDS file");
    const std::uint32_t height = word(bytes, 12);
    const std::uint32_t width = word(bytes, 16);
    const std::uint32_t levels = std::max(1u, word(bytes, 28));
    const std::uint32_t flags = word(bytes, 80);
    const std::uint32_t fourCc = word(bytes, 84);
    if (width == 0 || height == 0 || width > maxSide || height > maxSide || levels > static_cast<std::uint32_t>(std::bit_width(std::max(width, height))))
        return failure("the DDS size is invalid");
    if ((word(bytes, 112) & 0xFE00) != 0 || (word(bytes, 112) & 0x200000) != 0)
        return failure("cube maps and volume textures are not supported");
    std::size_t dataStart = headerEnd;
    std::uint32_t textureFormatCode = 0;
    if ((flags & fourCcFlag) != 0 && fourCc == dx10) {
        if (bytes.size() < dx10End)
            return failure("the DDS header is truncated");
        const std::uint32_t dxgi = word(bytes, 128);
        dataStart = dx10End;
        if (const auto found = std::ranges::find(dxgiFormats, dxgi, &std::pair<std::uint32_t, std::uint32_t>::first); found != dxgiFormats.end())
            textureFormatCode = found->second;
        else if (dxgi == 72 || dxgi == 75 || dxgi == 78 || dxgi == 99 || dxgi == 56)
            textureFormatCode = enhancedOnly | dxgi;
        else if (dxgi == 87 || dxgi == 91)
            textureFormatCode = 21;
        else if (dxgi == 28 || dxgi == 29)
            textureFormatCode = 32;
        else
            return failure("DXGI format " + std::to_string(dxgi) + " is not supported");
    } else if ((flags & fourCcFlag) != 0) {
        if (fourCc == 0x55344342)
            textureFormatCode = ati1;
        else if (fourCc == 0x55354342)
            textureFormatCode = ati2;
        else if (textureFormat(fourCc) && textureFormat(fourCc)->block)
            textureFormatCode = fourCc;
        else
            return failure("DDS format is not supported");
    } else {
        const std::uint32_t bits = word(bytes, 88);
        const std::array<std::uint32_t, 4> masks { word(bytes, 92), word(bytes, 96), word(bytes, 100), word(bytes, 104) };
        const auto found = std::ranges::find_if(uncompressed, [&](const Masks &candidate) {
            const bool alpha = (flags & alphaPixels) != 0 || (flags & alphaOnly) != 0;
            return candidate.bits == bits && candidate.masks[0] == masks[0] && candidate.masks[1] == masks[1] && candidate.masks[2] == masks[2] && (alpha ? candidate.masks[3] == masks[3] : candidate.masks[3] == 0);
        });
        if (found == uncompressed.end())
            return failure("DDS pixel layout is not supported");
        textureFormatCode = found->format;
    }
    const auto size = mipChainSize(textureFormatCode, width, height, levels);
    if (!size || *size > dataLimit || bytes.size() - dataStart < *size)
        return failure("the DDS file is shorter than its mip chain");
    const auto data = bytes.subspan(dataStart, static_cast<std::size_t>(*size));
    return Texture { std::move(name), width, height, levels, textureFormatCode, 0x20000014, std::vector<std::byte>(data.begin(), data.end()) };
}

Texture rgbaTexture(std::string name, std::uint32_t width, std::uint32_t height, std::span<const std::byte> rgba)
{
    std::uint32_t levels = 1;
    while ((width >> levels) > 0 || (height >> levels) > 0)
        ++levels;
    Texture texture { std::move(name), width, height, levels, 21, 0x20000014, {} };
    texture.data.reserve(static_cast<std::size_t>(*mipChainSize(21, width, height, levels)));
    std::vector<std::byte> current(rgba.begin(), rgba.end());
    std::uint32_t w = width;
    std::uint32_t h = height;
    for (std::uint32_t level = 0; level < levels; ++level) {
        for (std::size_t index = 0; index < std::size_t(w) * h; ++index) {
            texture.data.push_back(current[index * 4 + 2]);
            texture.data.push_back(current[index * 4 + 1]);
            texture.data.push_back(current[index * 4]);
            texture.data.push_back(current[index * 4 + 3]);
        }
        if (level + 1 == levels)
            break;
        const std::uint32_t nw = std::max(1u, w / 2);
        const std::uint32_t nh = std::max(1u, h / 2);
        std::vector<std::byte> next(std::size_t(nw) * nh * 4);
        for (std::uint32_t y = 0; y < nh; ++y) {
            for (std::uint32_t x = 0; x < nw; ++x) {
                for (int c = 0; c < 4; ++c) {
                    std::uint32_t sum = 0;
                    std::uint32_t count = 0;
                    for (std::uint32_t dy = 0; dy < 2; ++dy) {
                        for (std::uint32_t dx = 0; dx < 2; ++dx) {
                            const std::uint32_t sx = std::min(w - 1, x * 2 + dx);
                            const std::uint32_t sy = std::min(h - 1, y * 2 + dy);
                            sum += std::to_integer<std::uint32_t>(current[(std::size_t(sy) * w + sx) * 4 + c]);
                            ++count;
                        }
                    }
                    next[(std::size_t(y) * nw + x) * 4 + c] = static_cast<std::byte>((sum + count / 2) / count);
                }
            }
        }
        current = std::move(next);
        w = nw;
        h = nh;
    }
    return texture;
}

}
