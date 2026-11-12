#include "core/crypto/hash.h"
#include "core/io/ascii.h"
#include "core/rpf/rpf_reader.h"
#include "core/rsc/resource_file.h"
#include "core/rsc/rsc7.h"
#include "core/ytd/texture_dictionary.h"
#include "core/ytd/texture_format.h"
#include "support/real_install.h"
#include "support/real_keys.h"
#include "support/test_bytes.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestTextureDictionary : public QObject {
    Q_OBJECT
private slots:
    void formatTable();
    void readsHandBuiltDictionary();
    void writeThenReadIsIdentity();
    void writesSortedByHash();
    void hostileDictionariesAreRefused();
    void emptyDictionaryHasNoTextures();
    void unknownFormatIsNotWritten();
    void oversizedFieldsAreNotWritten();
    void resourceFileFlagsHoldPages();
    void realDictionariesRoundTrip();
    void readsHandBuiltEnhancedDictionary();
    void enhancedWriteThenReadIsIdentity();
    void unknownVersionIsRefused();
    void enhancedOnlyFormatsRoundTrip();
};

namespace {

void put(std::vector<std::byte> &bytes, std::size_t offset, std::uint64_t value, int size)
{
    for (int i = 0; i < size; ++i)
        bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}

std::vector<std::byte> handBuilt()
{
    std::vector<std::byte> pages(0x2000 + 0x1000);
    put(pages, 0x00, 0x40571500, 4);
    put(pages, 0x04, 1, 4);
    put(pages, 0x08, 0x500000d0, 8);
    put(pages, 0x18, 1, 4);
    put(pages, 0x20, 0x500000f0, 8);
    put(pages, 0x28, 1, 2);
    put(pages, 0x2A, 2, 2);
    put(pages, 0x30, 0x50000100, 8);
    put(pages, 0x38, 1, 2);
    put(pages, 0x3A, 2, 2);
    put(pages, 0x40, 0x40617568, 4);
    put(pages, 0x44, 1, 4);
    put(pages, 0x68, 0x50000110, 8);
    put(pages, 0x70, 1, 2);
    put(pages, 0x72, 0x80, 2);
    put(pages, 0x80, 0x2000b054, 4);
    put(pages, 0x90, 8, 2);
    put(pages, 0x92, 8, 2);
    put(pages, 0x94, 1, 2);
    put(pages, 0x96, 4, 2);
    put(pages, 0x98, 0x31545844, 4);
    put(pages, 0x9D, 2, 1);
    put(pages, 0xB0, 0x60000000, 8);
    put(pages, 0xD8, 0x0101, 2);
    const std::string name = "km_v_marmchair";
    put(pages, 0xF0, jenkinsHash(std::as_bytes(std::span(name.data(), name.size()))), 4);
    put(pages, 0x100, 0x50000040, 8);
    for (std::size_t i = 0; i < name.size(); ++i)
        pages[0x110 + i] = static_cast<std::byte>(name[i]);
    for (std::size_t i = 0; i < 40; ++i)
        pages[0x2000 + i] = static_cast<std::byte>(i + 1);
    return pages;
}

Texture texture(std::string name, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t levels, unsigned seed)
{
    Texture made { std::move(name), width, height, levels, format, 0x20000014, {} };
    made.data.resize(static_cast<std::size_t>(*mipChainSize(format, width, height, levels)));
    for (std::size_t i = 0; i < made.data.size(); ++i)
        made.data[i] = static_cast<std::byte>((i * 13 + seed) & 0xFF);
    return made;
}

Result<std::vector<Texture>> reread(const std::vector<std::byte> &file)
{
    const auto header = parseRsc7Header(std::span(file).first(16));
    if (!header)
        return std::unexpected(header.error());
    const auto pages = inflateResource(file);
    if (!pages)
        return std::unexpected(pages.error());
    return readTextureDictionary(*pages, rsc7SizeFromFlags(header->systemFlags), header->version);
}

std::vector<std::byte> handBuiltEnhanced()
{
    std::vector<std::byte> pages(0x2000 + 0x1000);
    put(pages, 0x00, 0x4068c308, 4);
    put(pages, 0x04, 1, 4);
    put(pages, 0x08, 0x500000c0, 8);
    put(pages, 0x18, 1, 4);
    put(pages, 0x20, 0x50000100, 8);
    put(pages, 0x28, 1, 2);
    put(pages, 0x2A, 2, 2);
    put(pages, 0x30, 0x500000e0, 8);
    put(pages, 0x38, 1, 2);
    put(pages, 0x3A, 2, 2);
    put(pages, 0x40, 0x406b7940, 4);
    put(pages, 0x44, 1, 4);
    put(pages, 0x48, 5, 4);
    put(pages, 0x4C, 16, 4);
    put(pages, 0x50, 0x00260208, 4);
    put(pages, 0x58, 8, 2);
    put(pages, 0x5A, 8, 2);
    put(pages, 0x5C, 0x4d010001, 4);
    put(pages, 0x60, 0x280200ff, 4);
    put(pages, 0x64, 0x00010000, 4);
    put(pages, 0x68, 0x500000f0, 8);
    put(pages, 0x70, 0x50000098, 8);
    put(pages, 0x78, 0x60000000, 8);
    put(pages, 0x80, 0x00800274, 4);
    put(pages, 0x84, 2, 4);
    put(pages, 0x98, 0x406b77d8, 4);
    const std::string name = "km_v_marmchair";
    for (std::size_t i = 0; i < name.size(); ++i)
        pages[0xf0 + i] = static_cast<std::byte>(name[i]);
    put(pages, 0x100, jenkinsHash(std::as_bytes(std::span(name.data(), name.size()))), 4);
    put(pages, 0xe0, 0x50000040, 8);
    for (std::size_t i = 0; i < 80; ++i)
        pages[0x2000 + i] = static_cast<std::byte>(i + 1);
    return pages;
}

}

void TestTextureDictionary::formatTable()
{
    QCOMPARE(std::string(textureFormat(0x31545844)->name), std::string("DXT1"));
    QCOMPARE(std::string(textureFormat(21)->name), std::string("A8R8G8B8"));
    QVERIFY(!textureFormat(12345));
    QCOMPARE(*mipChainSize(0x31545844, 512, 512, 8), std::uint64_t(0x2aaa8));
    QCOMPARE(*mipChainSize(0x35545844, 512, 512, 8), std::uint64_t(0x55550));
    QCOMPARE(*mipChainSize(0x31545844, 4, 4, 1), std::uint64_t(8));
    QCOMPARE(*mipChainSize(0x35545844, 256, 128, 9), std::uint64_t(0xaad0));
    QCOMPARE(*mipChainSize(21, 4, 2, 3), std::uint64_t(32 + 8 + 4));
    QVERIFY(!mipChainSize(12345, 4, 4, 1));
    QCOMPARE(*mipChainSize(21, 8192, 8192, 14), std::uint64_t(357913940));
}

void TestTextureDictionary::readsHandBuiltDictionary()
{
    const auto textures = readTextureDictionary(handBuilt(), 0x2000);
    QVERIFY2(textures, textures ? "" : textures.error().message.c_str());
    QCOMPARE(textures->size(), std::size_t(1));
    const Texture &first = textures->front();
    QCOMPARE(first.name, std::string("km_v_marmchair"));
    QCOMPARE(first.width, 8u);
    QCOMPARE(first.height, 8u);
    QCOMPARE(first.levels, 2u);
    QCOMPARE(first.format, 0x31545844u);
    QCOMPARE(first.usage, 0x2000b054u);
    QCOMPARE(first.data.size(), std::size_t(32 + 8));
    QCOMPARE(first.data[0], std::byte { 1 });
    QCOMPARE(first.data[39], std::byte { 40 });
}

void TestTextureDictionary::writeThenReadIsIdentity()
{
    const std::vector<Texture> textures {
        texture("diffuse", 0x31545844, 512, 256, 8, 1),
        texture("normal_n", 0x32495441, 128, 128, 6, 2),
        texture("tiny", 0x35545844, 4, 4, 1, 3),
        texture("argb", 21, 64, 32, 7, 4),
    };
    const auto file = writeTextureDictionary(textures);
    QVERIFY2(file, file ? "" : file.error().message.c_str());
    const auto header = parseRsc7Header(std::span(*file).first(16));
    QVERIFY(header);
    QCOMPARE(header->version, 13u);
    const auto read = reread(*file);
    QVERIFY2(read, read ? "" : read.error().message.c_str());
    QCOMPARE(read->size(), textures.size());
    for (const Texture &expected : textures) {
        const auto found = std::ranges::find(*read, expected.name, &Texture::name);
        QVERIFY(found != read->end());
        QCOMPARE(found->width, expected.width);
        QCOMPARE(found->height, expected.height);
        QCOMPARE(found->levels, expected.levels);
        QCOMPARE(found->format, expected.format);
        QCOMPARE(found->usage, expected.usage);
        QVERIFY(found->data == expected.data);
    }
}

void TestTextureDictionary::writesSortedByHash()
{
    const std::vector<Texture> textures { texture("zzz", 0x31545844, 4, 4, 1, 1), texture("aaa", 0x31545844, 4, 4, 1, 2), texture("Mixed_Case", 0x31545844, 4, 4, 1, 3) };
    const auto read = reread(*writeTextureDictionary(textures));
    QVERIFY(read);
    std::vector<std::uint32_t> hashes;
    for (const Texture &item : *read) {
        const std::string lower = asciiLower(item.name);
        hashes.push_back(jenkinsHash(std::as_bytes(std::span(lower.data(), lower.size()))));
    }
    QVERIFY(std::ranges::is_sorted(hashes));
    QVERIFY(std::ranges::find(*read, "Mixed_Case", &Texture::name) != read->end());
}

void TestTextureDictionary::hostileDictionariesAreRefused()
{
    auto pages = handBuilt();
    put(pages, 0x30, 0x50100000, 8);
    QVERIFY(!readTextureDictionary(pages, 0x2000));
    pages = handBuilt();
    put(pages, 0x38, 0xFFFF, 2);
    QVERIFY(!readTextureDictionary(pages, 0x2000));
    pages = handBuilt();
    put(pages, 0xB0, 0x60000FF0, 8);
    QVERIFY(!readTextureDictionary(pages, 0x2000));
    pages = handBuilt();
    put(pages, 0x68, 0x50001FFE, 8);
    pages[0x1FFE] = std::byte { 'a' };
    pages[0x1FFF] = std::byte { 'b' };
    QVERIFY(!readTextureDictionary(pages, 0x2000));
    pages = handBuilt();
    put(pages, 0x9D, 40, 1);
    QVERIFY(!readTextureDictionary(pages, 0x2000));
    QVERIFY(!readTextureDictionary(std::vector<std::byte>(0x10), 0x10));
}

void TestTextureDictionary::emptyDictionaryHasNoTextures()
{
    std::vector<std::byte> pages(0x2000);
    put(pages, 0x00, 0x405713f0, 4);
    put(pages, 0x04, 1, 4);
    put(pages, 0x08, 0x50000040, 8);
    put(pages, 0x18, 1, 4);
    const auto textures = readTextureDictionary(pages, 0x2000);
    QVERIFY2(textures, textures ? "" : textures.error().message.c_str());
    QVERIFY(textures->empty());
    const auto written = writeTextureDictionary(*textures);
    QVERIFY(written);
    QVERIFY(reread(*written)->empty());
}

void TestTextureDictionary::unknownFormatIsNotWritten()
{
    Texture odd = texture("ok", 0x31545844, 4, 4, 1, 1);
    odd.format = 12345;
    const auto written = writeTextureDictionary(std::vector<Texture> { odd });
    QVERIFY(!written);
    QCOMPARE(written.error().message, std::string("ok uses texture format 12345, which ARCHON cannot rewrite."));
}

void TestTextureDictionary::oversizedFieldsAreNotWritten()
{
    Texture wide { "wide", 70000, 1, 1, 21, 0x20000014, std::vector<std::byte>(280000) };
    QCOMPARE(writeTextureDictionary(std::vector<Texture> { wide }).error().message, std::string("wide is larger than 65535 pixels or its row is too long to store"));
    Texture rowTooLong { "row", 16384, 1, 1, 21, 0x20000014, std::vector<std::byte>(65536) };
    QVERIFY(!writeTextureDictionary(std::vector<Texture> { rowTooLong }));
    Texture tooManyLevels = texture("levels", 0x31545844, 4, 4, 3, 1);
    tooManyLevels.levels = 4;
    tooManyLevels.data.resize(static_cast<std::size_t>(*mipChainSize(0x31545844, 4, 4, 4)));
    QCOMPARE(writeTextureDictionary(std::vector<Texture> { tooManyLevels }).error().message, std::string("levels has more mip levels than its size allows"));
}

void TestTextureDictionary::resourceFileFlagsHoldPages()
{
    QCOMPARE(rsc7SizeFromFlags(singlePageFlags(0x2000, 0)), std::uint64_t(0x2000));
    QCOMPARE(rsc7SizeFromFlags(singlePageFlags(0x1000000, 0xD)), std::uint64_t(0x1000000));
    QCOMPARE(rsc7SizeFromFlags(singlePageFlags(0x8000000, 0xD)), std::uint64_t(0x8000000));
    QCOMPARE(singlePageFlags(0x2000, 0xD) >> 28, 0xDu);
    const std::vector<std::byte> system(0x2000, std::byte { 1 });
    const std::vector<std::byte> graphics(0x4000, std::byte { 2 });
    const auto file = *buildResourceFile(13, system, graphics);
    const auto header = parseRsc7Header(std::span(file).first(16));
    QVERIFY(header);
    QCOMPARE(header->version, 13u);
    QCOMPARE(rsc7SizeFromFlags(header->systemFlags), std::uint64_t(0x2000));
    QCOMPARE(rsc7SizeFromFlags(header->graphicsFlags), std::uint64_t(0x4000));
    const auto pages = inflateResource(file);
    QVERIFY(pages);
    QCOMPARE(pages->size(), std::size_t(0x6000));
    QCOMPARE((*pages)[0x1FFF], std::byte { 1 });
    QCOMPARE((*pages)[0x2000], std::byte { 2 });
}

void TestTextureDictionary::realDictionariesRoundTrip()
{
    ARCHON_REQUIRE_REAL();
    const Keys *keys = realKeys();
    QVERIFY(keys);
    const auto outer = RpfReader::open(openFileSource(*realGameDir() / "x64g.rpf"), "x64g.rpf", keys);
    QVERIFY(outer);
    const auto reader = outer->openNested(*outer->archive().find("levels/gta5/generic/cutsobjects.rpf"));
    QVERIFY2(reader, reader ? "" : reader.error().message.c_str());
    std::size_t checked = 0;
    std::size_t textures = 0;
    const auto &entries = reader->archive().entries();
    for (std::uint32_t index = 0; index < entries.size(); ++index) {
        if (entries[index].kind != RpfEntryKind::Resource || !asciiLower(entries[index].name).ends_with(".ytd"))
            continue;
        const auto pages = reader->fileData(index);
        QVERIFY(pages);
        const std::uint32_t version = rsc7VersionFromFlags(entries[index].systemFlags, entries[index].graphicsFlags);
        const auto read = readTextureDictionary(*pages, rsc7SizeFromFlags(entries[index].systemFlags), version);
        QVERIFY2(read, (entries[index].name + ": " + (read ? "" : read.error().message)).c_str());
        const auto written = writeTextureDictionary(*read, version);
        QVERIFY2(written, written ? "" : written.error().message.c_str());
        const auto again = reread(*written);
        QVERIFY2(again, again ? "" : again.error().message.c_str());
        QCOMPARE(again->size(), read->size());
        for (const Texture &expected : *read) {
            const auto found = std::ranges::find(*again, expected.name, &Texture::name);
            QVERIFY(found != again->end());
            QVERIFY(found->data == expected.data);
            QCOMPARE(found->usage, expected.usage);
        }
        textures += read->size();
        ++checked;
    }
    QVERIFY(checked > 50);
    qInfo("cutsobjects.rpf: %zu dictionaries, %zu textures round-tripped", checked, textures);
}

void TestTextureDictionary::readsHandBuiltEnhancedDictionary()
{
    const auto textures = readTextureDictionary(handBuiltEnhanced(), 0x2000, 5);
    QVERIFY2(textures, textures ? "" : textures.error().message.c_str());
    QCOMPARE(textures->size(), std::size_t(1));
    const Texture &first = textures->front();
    QCOMPARE(first.name, std::string("km_v_marmchair"));
    QCOMPARE(first.width, 8u);
    QCOMPARE(first.levels, 2u);
    QCOMPARE(first.format, 0x35545844u);
    QCOMPARE(first.usage, 0x00800274u);
    QCOMPARE(first.data.size(), std::size_t(80));
    QCOMPARE(first.data[79], std::byte { 80 });
}

void TestTextureDictionary::enhancedWriteThenReadIsIdentity()
{
    const std::vector<Texture> textures {
        texture("diffuse", 0x31545844, 512, 256, 8, 1),
        texture("normal_n", 0x32495441, 128, 128, 6, 2),
        texture("argb", 21, 64, 32, 7, 4),
        texture("bc7", 0x20374342, 16, 16, 3, 5),
    };
    const auto file = writeTextureDictionary(textures, 5);
    QVERIFY2(file, file ? "" : file.error().message.c_str());
    QCOMPARE(parseRsc7Header(std::span(*file).first(16))->version, 5u);
    const auto read = reread(*file);
    QVERIFY2(read, read ? "" : read.error().message.c_str());
    for (const Texture &expected : textures) {
        const auto found = std::ranges::find(*read, expected.name, &Texture::name);
        QVERIFY(found != read->end());
        QCOMPARE(found->format, expected.format);
        QCOMPARE(found->levels, expected.levels);
        QCOMPARE(found->usage, 0x00800214u);
        QVERIFY(found->data == expected.data);
    }
}

void TestTextureDictionary::unknownVersionIsRefused()
{
    QCOMPARE(readTextureDictionary(handBuilt(), 0x2000, 7).error().message, std::string("texture dictionary version 7 is not supported"));
    QVERIFY(!writeTextureDictionary(std::vector<Texture> {}, 7));
}

void TestTextureDictionary::enhancedOnlyFormatsRoundTrip()
{
    const std::uint32_t bc3Srgb = 0x80000000u | 78;
    const std::uint32_t bc7Srgb = 0x80000000u | 99;
    const std::uint32_t r16 = 0x80000000u | 56;
    QCOMPARE(std::string(textureFormat(bc3Srgb)->name), std::string("DXT5 sRGB"));
    QCOMPARE(*mipChainSize(r16, 4, 4, 1), std::uint64_t(32));
    const std::vector<Texture> textures { texture("srgb", bc3Srgb, 16, 16, 3, 1), texture("bc7s", bc7Srgb, 8, 8, 2, 2), texture("height", r16, 8, 4, 2, 3) };
    const auto read = reread(*writeTextureDictionary(textures, 5));
    QVERIFY(read);
    for (const Texture &expected : textures) {
        const auto found = std::ranges::find(*read, expected.name, &Texture::name);
        QCOMPARE(found->format, expected.format);
        QVERIFY(found->data == expected.data);
    }
    const auto legacy = writeTextureDictionary(textures, 13);
    QVERIFY(!legacy);
    QCOMPARE(legacy.error().message, std::string("srgb uses texture format DXT5 sRGB, which a Legacy texture dictionary cannot hold."));
}

QTEST_APPLESS_MAIN(TestTextureDictionary)
#include "tst_texture_dictionary.moc"
