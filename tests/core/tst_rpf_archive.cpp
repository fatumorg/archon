#include "core/game/game_install.h"
#include "core/rpf/rpf_archive.h"
#include "support/real_install.h"
#include "support/real_keys.h"
#include "support/rpf_builder.h"
#include "support/test_bytes.h"

#include <QTest>

#include <algorithm>
#include <fstream>
#include <memory>

using namespace archon;
using namespace archon::test;

namespace {

constexpr std::size_t entryTable = 16;
constexpr std::size_t entrySize = 16;
constexpr std::uint32_t dataDirectory = 1;
constexpr std::uint32_t compressedFile = 4;

std::vector<std::byte> sampleArchive(RpfEncryption encryption = RpfEncryption::None)
{
    RpfBuilder builder;
    builder.addBinary("data/a.txt", toVector("hello"), false);
    builder.addBinary("data/b.bin", std::vector<std::byte>(5000, std::byte { 'z' }), true);
    builder.addResource("x.ytd", std::vector<std::byte>(32, std::byte { 7 }), 0x10, 0x20);
    return builder.build(encryption);
}

void patch(std::vector<std::byte> &archive, std::size_t offset, std::initializer_list<int> values)
{
    for (const int value : values)
        archive[offset++] = static_cast<std::byte>(value);
}

Result<RpfArchive> parseWhole(const std::vector<std::byte> &archive, const Keys *keys = nullptr)
{
    return RpfArchive::parse(archive, "test.rpf", archive.size(), keys);
}

Result<RpfArchive> parseFile(const std::filesystem::path &file, const Keys *keys)
{
    std::ifstream stream(file, std::ios::binary);
    std::vector<std::byte> toc(16);
    stream.read(reinterpret_cast<char *>(toc.data()), 16);
    const auto size = RpfArchive::tocSize(toc);
    if (!stream || !size)
        return std::unexpected(Error { "test", 0, "cannot read header" });
    toc.resize(*size);
    stream.read(reinterpret_cast<char *>(toc.data()) + 16, static_cast<std::streamsize>(*size - 16));
    if (!stream)
        return std::unexpected(Error { "test", 0, "cannot read table of contents" });
    return RpfArchive::parse(toc, file.filename().string(), std::filesystem::file_size(file), keys);
}

std::array<int, 3> countKinds(const RpfArchive &archive)
{
    std::array<int, 3> counts {};
    for (const RpfEntry &entry : archive.entries())
        ++counts[static_cast<std::size_t>(entry.kind)];
    return counts;
}

}

class TestRpfArchive : public QObject {
    Q_OBJECT
private slots:
    void parsesSyntheticArchive();
    void parsesAesEncryptedToc();
    void rejectsBadMagic();
    void rejectsTruncatedToc();
    void rejectsChildRangeOutsideTable();
    void rejectsSelfReferencingDirectory();
    void rejectsUnreachableEntry();
    void rejectsDirectoriesNestedTooDeep();
    void rejectsNameOffsetOutsideNames();
    void rejectsEntryPastEndOfArchive();
    void rejectsOversizedCounts();
    void encryptedArchiveWithoutKeysFails();
    void parsesRealArchives();
};

void TestRpfArchive::parsesSyntheticArchive()
{
    const auto bytes = sampleArchive();
    const auto parsed = parseWhole(bytes);
    QVERIFY2(parsed, parsed ? "" : parsed.error().message.c_str());
    const RpfArchive &archive = *parsed;
    QCOMPARE(archive.entries().size(), std::size_t(5));
    QCOMPARE(archive.encryption(), RpfEncryption::None);
    QCOMPARE(archive.entries()[0].kind, RpfEntryKind::Directory);
    QCOMPARE(archive.entries()[0].childCount, 2u);
    QCOMPARE(archive.entries()[0].parent, 0u);
    QCOMPARE(archive.find(""), std::optional<std::uint32_t>(0));

    const auto text = archive.find("data/a.txt");
    QVERIFY(text);
    QCOMPARE(archive.entries()[*text].kind, RpfEntryKind::Binary);
    QCOMPARE(archive.entries()[*text].storedSize, 0u);
    QCOMPARE(archive.entries()[*text].uncompressedSize, 5u);
    QCOMPARE(archive.entries()[*text].parent, dataDirectory);
    QCOMPARE(archive.entries()[*text].offset % 512, std::uint64_t(0));
    QVERIFY(!archive.entries()[*text].encrypted);

    const auto packed = archive.find("DATA/B.BIN");
    QVERIFY(packed);
    QCOMPARE(*packed, compressedFile);
    QVERIFY(archive.entries()[*packed].storedSize > 0);
    QCOMPARE(archive.entries()[*packed].uncompressedSize, 5000u);
    QCOMPARE(archive.path(*packed), std::string("data/b.bin"));

    const auto resource = archive.find("x.ytd");
    QVERIFY(resource);
    QCOMPARE(archive.entries()[*resource].kind, RpfEntryKind::Resource);
    QCOMPARE(archive.entries()[*resource].systemFlags, 0x10u);
    QCOMPARE(archive.entries()[*resource].graphicsFlags, 0x20u);
    QVERIFY(archive.entries()[*resource].storedSize > 16);

    QVERIFY(!archive.find("nope"));
    QVERIFY(!archive.find("data/a.txt/deeper"));
    QCOMPARE(archive.path(0), std::string());
    QCOMPARE(RpfArchive::tocSize(std::span(bytes).first(16)).value(), std::size_t(16 + 5 * 16 + 32));

    const auto open = parseWhole(sampleArchive(RpfEncryption::Open));
    QVERIFY(open);
    QCOMPARE(open->encryption(), RpfEncryption::Open);
}

void TestRpfArchive::parsesAesEncryptedToc()
{
    auto keys = std::make_unique<Keys>();
    for (std::size_t i = 0; i < keys->aes.size(); ++i)
        keys->aes[i] = static_cast<std::byte>(i);
    auto bytes = sampleArchive();
    const std::size_t tocEnd = RpfArchive::tocSize(std::span(bytes).first(16)).value();
    const auto entries = aesEcbEncrypt(std::span(bytes).subspan(entryTable, 5 * entrySize), keys->aes);
    const auto names = aesEcbEncrypt(std::span(bytes).subspan(entryTable + 5 * entrySize, tocEnd - entryTable - 5 * entrySize), keys->aes);
    std::ranges::copy(entries, bytes.begin() + entryTable);
    std::ranges::copy(names, bytes.begin() + entryTable + 5 * entrySize);
    patch(bytes, 12, { 0xF9, 0xFF, 0xFF, 0x0F });
    const auto parsed = parseWhole(bytes, keys.get());
    QVERIFY2(parsed, parsed ? "" : parsed.error().message.c_str());
    QCOMPARE(parsed->encryption(), RpfEncryption::Aes);
    QVERIFY(parsed->find("data/b.bin"));
    QVERIFY(!parseWhole(bytes));
}

void TestRpfArchive::rejectsBadMagic()
{
    auto bytes = sampleArchive();
    patch(bytes, 0, { '6' });
    const auto parsed = parseWhole(bytes);
    QVERIFY(!parsed);
    QCOMPARE(parsed.error().offset, std::uint64_t(0));
    QCOMPARE(parsed.error().format, std::string("rpf7"));
    QVERIFY(!RpfArchive::tocSize(std::span(bytes).first(16)));
}

void TestRpfArchive::rejectsTruncatedToc()
{
    const auto bytes = sampleArchive();
    QVERIFY(!RpfArchive::parse(std::span(bytes).first(entryTable + 8), "test.rpf", bytes.size(), nullptr));
    QVERIFY(!RpfArchive::parse(std::span(bytes).first(8), "test.rpf", bytes.size(), nullptr));
    QVERIFY(!RpfArchive::tocSize(std::span(bytes).first(8)));
}

void TestRpfArchive::rejectsChildRangeOutsideTable()
{
    auto bytes = sampleArchive();
    patch(bytes, entryTable + 12, { 0xE8, 0x03, 0, 0 });
    QVERIFY(!parseWhole(bytes));
    bytes = sampleArchive();
    patch(bytes, entryTable + 8, { 0xFF, 0xFF, 0xFF, 0xFF });
    QVERIFY(!parseWhole(bytes));
}

void TestRpfArchive::rejectsSelfReferencingDirectory()
{
    auto bytes = sampleArchive();
    patch(bytes, entryTable + dataDirectory * entrySize + 8, { 1, 0, 0, 0 });
    QVERIFY(!parseWhole(bytes));
    bytes = sampleArchive();
    patch(bytes, entryTable + dataDirectory * entrySize + 8, { 0, 0, 0, 0 });
    QVERIFY(!parseWhole(bytes));
}

void TestRpfArchive::rejectsDirectoriesNestedTooDeep()
{
    const auto chain = [](int depth) {
        std::string path = "d";
        for (int level = 1; level < depth; ++level)
            path += "/d";
        RpfBuilder builder;
        builder.addBinary(path + "/a.txt", toVector("hello"), false);
        return builder.build();
    };
    QVERIFY(parseWhole(chain(64)));
    const auto tooDeep = parseWhole(chain(65));
    QVERIFY(!tooDeep);
    QCOMPARE(tooDeep.error().message, std::string("directories are nested too deep"));
}

void TestRpfArchive::rejectsUnreachableEntry()
{
    auto bytes = sampleArchive();
    patch(bytes, entryTable + dataDirectory * entrySize + 12, { 1, 0, 0, 0 });
    QVERIFY(!parseWhole(bytes));
}

void TestRpfArchive::rejectsNameOffsetOutsideNames()
{
    auto bytes = sampleArchive();
    patch(bytes, entryTable + 3 * entrySize, { 0xFF, 0xFF });
    QVERIFY(!parseWhole(bytes));
}

void TestRpfArchive::rejectsEntryPastEndOfArchive()
{
    auto bytes = sampleArchive();
    patch(bytes, entryTable + compressedFile * entrySize + 5, { 0, 0, 0x10 });
    QVERIFY(!parseWhole(bytes));
    bytes = sampleArchive();
    QVERIFY(!RpfArchive::parse(bytes, "test.rpf", bytes.size() - 1, nullptr));
    QVERIFY(RpfArchive::parse(bytes, "test.rpf", bytes.size(), nullptr));
}

void TestRpfArchive::rejectsOversizedCounts()
{
    auto header = fromHex("37465052ffffff7f1000000000000000");
    QVERIFY(!RpfArchive::tocSize(header));
    QVERIFY(!RpfArchive::parse(header, "test.rpf", 16, nullptr));
    header = fromHex("3746505201000000ffffff7f00000000");
    QVERIFY(!RpfArchive::tocSize(header));
    header = fromHex("37465052000000001000000000000000");
    QVERIFY(!RpfArchive::tocSize(header));
}

void TestRpfArchive::encryptedArchiveWithoutKeysFails()
{
    auto bytes = sampleArchive();
    patch(bytes, 12, { 0xFF, 0xFF, 0xEF, 0x0F });
    const auto parsed = parseWhole(bytes);
    QVERIFY(!parsed);
    QCOMPARE(parsed.error().message, std::string("archive is encrypted and no keys are loaded"));
}

void TestRpfArchive::parsesRealArchives()
{
    ARCHON_REQUIRE_REAL();
    const Keys *keys = realKeys();
    QVERIFY(keys);

    const auto x64a = parseFile(*realGameDir() / "x64a.rpf", keys);
    QVERIFY2(x64a, x64a ? "" : x64a.error().message.c_str());
    const bool enhanced = openGameInstall(*realGameDir())->edition == GameEdition::Enhanced;
    QCOMPARE(x64a->entries().size(), std::size_t(enhanced ? 641 : 656));
    QCOMPARE(countKinds(*x64a), enhanced ? (std::array<int, 3> { 31, 187, 423 }) : (std::array<int, 3> { 32, 196, 428 }));
    QCOMPARE(x64a->encryption(), RpfEncryption::Ng);
    const auto data = x64a->find("data");
    QVERIFY(data);
    QCOMPARE(x64a->entries()[*data].kind, RpfEntryKind::Directory);

    const auto common = parseFile(*realGameDir() / "common.rpf", keys);
    QVERIFY2(common, common ? "" : common.error().message.c_str());
    QCOMPARE(common->entries().size(), std::size_t(enhanced ? 705 : 1421));
    QCOMPARE(countKinds(*common), enhanced ? (std::array<int, 3> { 40, 665, 0 }) : (std::array<int, 3> { 43, 1378, 0 }));

    const auto update = parseFile(*realGameDir() / "update" / "update.rpf", keys);
    QVERIFY2(update, update ? "" : update.error().message.c_str());
    const auto dlcList = update->find("common/data/dlclist.xml");
    QVERIFY(dlcList);
    QCOMPARE(update->entries()[*dlcList].kind, RpfEntryKind::Binary);
}

QTEST_APPLESS_MAIN(TestRpfArchive)
#include "tst_rpf_archive.moc"
