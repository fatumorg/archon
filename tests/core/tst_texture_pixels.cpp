#include "core/ytd/texture_format.h"
#include "core/ytd/texture_pixels.h"
#include "support/test_bytes.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestTexturePixels : public QObject {
    Q_OBJECT
private slots:
    void decodesBc1Block();
    void decodesSmallMipOfBlockFormat();
    void swizzlesUncompressedFormats();
    void ddsRoundTripsEveryFormat();
    void readsDx10Headers();
    void buildsMipmapsByAveraging();
    void malformedDdsIsRefused();
    void unknownFormatIsNotExported();
};

namespace {

std::vector<std::byte> bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> out;
    for (const int value : values)
        out.push_back(static_cast<std::byte>(value));
    return out;
}

Texture filled(std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t levels)
{
    Texture texture { "t", width, height, levels, format, 0x20000014, {} };
    texture.data.resize(static_cast<std::size_t>(*mipChainSize(format, width, height, levels)));
    for (std::size_t i = 0; i < texture.data.size(); ++i)
        texture.data[i] = static_cast<std::byte>(i * 7 + 3);
    return texture;
}

std::array<int, 4> pixel(const std::vector<std::byte> &rgba, std::size_t index)
{
    return { std::to_integer<int>(rgba[index * 4]), std::to_integer<int>(rgba[index * 4 + 1]), std::to_integer<int>(rgba[index * 4 + 2]), std::to_integer<int>(rgba[index * 4 + 3]) };
}

std::vector<std::byte> dx10Dds(std::uint32_t dxgi, std::uint32_t width, std::uint32_t height, std::size_t dataSize)
{
    std::vector<std::byte> dds(4 + 124 + 20 + dataSize);
    const auto put = [&dds](std::size_t offset, std::uint32_t value) {
        for (int i = 0; i < 4; ++i)
            dds[offset + i] = static_cast<std::byte>(value >> (8 * i));
    };
    put(0, 0x20534444);
    put(4, 124);
    put(8, 0x1007);
    put(12, height);
    put(16, width);
    put(28, 1);
    put(76, 32);
    put(80, 4);
    put(84, 0x30315844);
    put(108, 0x1000);
    put(128, dxgi);
    put(132, 3);
    put(140, 1);
    return dds;
}

}

void TestTexturePixels::decodesBc1Block()
{
    Texture texture { "b", 4, 4, 1, 0x31545844, 0, bytes({ 0x00, 0xF8, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x55 }) };
    const auto rgba = decodeRgba(texture, 0);
    QVERIFY2(rgba, rgba ? "" : rgba.error().message.c_str());
    QCOMPARE(rgba->size(), std::size_t(64));
    QCOMPARE(pixel(*rgba, 0), (std::array<int, 4> { 255, 0, 0, 255 }));
    QCOMPARE(pixel(*rgba, 12), (std::array<int, 4> { 0, 0, 255, 255 }));
}

void TestTexturePixels::decodesSmallMipOfBlockFormat()
{
    Texture texture = filled(0x35545844, 8, 8, 4);
    for (std::uint32_t level = 0; level < 4; ++level) {
        const auto rgba = decodeRgba(texture, level);
        QVERIFY(rgba);
        const std::uint32_t side = std::max(1u, 8u >> level);
        QCOMPARE(rgba->size(), std::size_t(side * side * 4));
    }
    QVERIFY(!decodeRgba(texture, 4));
    texture.format = 99999;
    QVERIFY(!decodeRgba(texture, 0));
}

void TestTexturePixels::swizzlesUncompressedFormats()
{
    QCOMPARE(pixel(*decodeRgba({ "a", 1, 1, 1, 21, 0, bytes({ 1, 2, 3, 4 }) }, 0), 0), (std::array<int, 4> { 3, 2, 1, 4 }));
    QCOMPARE(pixel(*decodeRgba({ "a", 1, 1, 1, 22, 0, bytes({ 1, 2, 3, 4 }) }, 0), 0), (std::array<int, 4> { 3, 2, 1, 255 }));
    QCOMPARE(pixel(*decodeRgba({ "a", 1, 1, 1, 32, 0, bytes({ 1, 2, 3, 4 }) }, 0), 0), (std::array<int, 4> { 1, 2, 3, 4 }));
    QCOMPARE(pixel(*decodeRgba({ "a", 1, 1, 1, 50, 0, bytes({ 77 }) }, 0), 0), (std::array<int, 4> { 77, 77, 77, 255 }));
    QCOMPARE(pixel(*decodeRgba({ "a", 1, 1, 1, 28, 0, bytes({ 66 }) }, 0), 0), (std::array<int, 4> { 255, 255, 255, 66 }));
    QCOMPARE(pixel(*decodeRgba({ "a", 1, 1, 1, 23, 0, bytes({ 0x00, 0xF8 }) }, 0), 0), (std::array<int, 4> { 255, 0, 0, 255 }));
    QCOMPARE(pixel(*decodeRgba({ "a", 1, 1, 1, 25, 0, bytes({ 0x1F, 0x00 }) }, 0), 0), (std::array<int, 4> { 0, 0, 255, 0 }));
}

void TestTexturePixels::ddsRoundTripsEveryFormat()
{
    for (const std::uint32_t format : { 0x31545844u, 0x33545844u, 0x35545844u, 0x31495441u, 0x32495441u, 0x20374342u, 21u, 22u, 23u, 25u, 28u, 32u, 50u, 0x80000000u | 78, 0x80000000u | 99, 0x80000000u | 72, 0x80000000u | 56 }) {
        const Texture original = filled(format, 64, 16, 5);
        const auto dds = writeDds(original);
        QVERIFY(dds);
        QVERIFY(dds->size() > original.data.size());
        const auto read = readDds(*dds, "again");
        QVERIFY2(read, (std::string(textureFormat(format)->name) + ": " + (read ? "" : read.error().message)).c_str());
        QCOMPARE(read->name, std::string("again"));
        QCOMPARE(read->format, format);
        QCOMPARE(read->width, 64u);
        QCOMPARE(read->height, 16u);
        QCOMPARE(read->levels, 5u);
        QVERIFY(read->data == original.data);
    }
}

void TestTexturePixels::readsDx10Headers()
{
    const auto bc1 = readDds(dx10Dds(71, 8, 8, 32), "x");
    QVERIFY2(bc1, bc1 ? "" : bc1.error().message.c_str());
    QCOMPARE(bc1->format, 0x31545844u);
    const auto bc7 = readDds(dx10Dds(98, 4, 4, 16), "x");
    QVERIFY(bc7);
    QCOMPARE(bc7->format, 0x20374342u);
    const auto bgra = readDds(dx10Dds(87, 2, 2, 16), "x");
    QVERIFY(bgra);
    QCOMPARE(bgra->format, 21u);
    QVERIFY(!readDds(dx10Dds(10, 4, 4, 128), "x"));
}

void TestTexturePixels::buildsMipmapsByAveraging()
{
    const std::vector<std::byte> rgba = bytes({ 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0 });
    const Texture texture = rgbaTexture("made", 2, 2, rgba);
    QCOMPARE(texture.format, 21u);
    QCOMPARE(texture.levels, 2u);
    QCOMPARE(texture.data.size(), std::size_t(20));
    QCOMPARE(texture.data[4], std::byte { 255 });
    QCOMPARE(texture.data[16], std::byte { 128 });
    QCOMPARE(texture.data[19], std::byte { 128 });
    const Texture wide = rgbaTexture("wide", 256, 128, std::vector<std::byte>(256 * 128 * 4, std::byte { 9 }));
    QCOMPARE(wide.levels, 9u);
    QCOMPARE(wide.data.size(), std::size_t(*mipChainSize(21, 256, 128, 9)));
    QCOMPARE(wide.data.back(), std::byte { 9 });
}

void TestTexturePixels::malformedDdsIsRefused()
{
    QVERIFY(!readDds(toVector("DDS "), "x"));
    QVERIFY(!readDds(std::vector<std::byte>(200), "x"));
    auto truncated = *writeDds(filled(0x31545844, 64, 64, 7));
    truncated.resize(truncated.size() - 1);
    QVERIFY(!readDds(truncated, "x"));
    auto cube = *writeDds(filled(0x31545844, 4, 4, 1));
    cube[4 + 108] = std::byte { 0x00 };
    cube[4 + 109] = std::byte { 0xFE };
    QVERIFY(!readDds(cube, "x"));
    auto deep = *writeDds(filled(0x31545844, 4, 4, 3));
    deep[28] = std::byte { 16 };
    deep.resize(deep.size() + 64);
    QVERIFY(!readDds(deep, "x"));
    auto zero = *writeDds(filled(0x31545844, 4, 4, 1));
    zero[12] = std::byte { 0 };
    zero[13] = std::byte { 0 };
    QVERIFY(!readDds(zero, "x"));
}

void TestTexturePixels::unknownFormatIsNotExported()
{
    QVERIFY(!writeDds({ "odd", 4, 4, 1, 12345, 0, {} }));
    QVERIFY(writeDds(filled(0x31545844, 4, 4, 1)));
}

QTEST_APPLESS_MAIN(TestTexturePixels)
#include "tst_texture_pixels.moc"
