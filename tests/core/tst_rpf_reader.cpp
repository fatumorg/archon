#include "core/rpf/rpf_reader.h"
#include "core/io/ascii.h"
#include "core/rsc/rsc7.h"
#include "support/memory_source.h"
#include "support/real_install.h"
#include "support/real_keys.h"
#include "support/rpf_builder.h"
#include "support/test_bytes.h"

#include <QTemporaryDir>
#include <QTest>

#include <fstream>

using namespace archon;
using namespace archon::test;

namespace {

constexpr std::size_t compressedFileEntry = 16 + 4 * 16;
const std::vector<std::byte> pages(0x400, std::byte { 7 });

std::vector<std::byte> sampleArchive()
{
    RpfBuilder builder;
    builder.addBinary("data/a.txt", toVector("hello"), false);
    builder.addBinary("data/b.bin", std::vector<std::byte>(5000, std::byte { 'z' }), true);
    builder.addResource("x.ytd", pages, 0x08000000, 0x08000000);
    return builder.build();
}

Result<RpfReader> openBytes(std::vector<std::byte> bytes)
{
    return RpfReader::open(memorySource(std::move(bytes)), "test.rpf", nullptr);
}

Result<std::vector<std::byte>> dataAt(const RpfReader &reader, std::string_view path)
{
    const auto index = reader.archive().find(path);
    if (!index)
        return std::unexpected(Error { "test", 0, "no such entry" });
    return reader.fileData(*index);
}

Result<RpfReader> openReal(const std::filesystem::path &relative)
{
    return RpfReader::open(openFileSource(*realGameDir() / relative), relative.filename().string(), realKeys());
}

}

class TestRpfReader : public QObject {
    Q_OBJECT
private slots:
    void readsSyntheticEntries();
    void readsSyntheticResource();
    void readsFromFileSource();
    void subSourceStaysInsideItsRange();
    void readsNestedArchive();
    void rejectsDeepNesting();
    void rejectsLyingUncompressedSize();
    void rejectsLyingResource();
    void rejectsTruncatedData();
    void encryptedEntryWithoutKeysFails();
    void readsRealBinaryEntry();
    void readsRealResourceEntry();
    void opensRealNestedArchive();
};

void TestRpfReader::readsSyntheticEntries()
{
    const auto reader = openBytes(sampleArchive());
    QVERIFY2(reader, reader ? "" : reader.error().message.c_str());
    QCOMPARE(reader->archive().entries().size(), std::size_t(5));
    QCOMPARE(toText(dataAt(*reader, "data/a.txt").value()), std::string("hello"));
    QCOMPARE(dataAt(*reader, "data/b.bin").value(), std::vector<std::byte>(5000, std::byte { 'z' }));
    QCOMPARE(reader->storedFile(*reader->archive().find("data/b.bin")).value(), std::vector<std::byte>(5000, std::byte { 'z' }));
    QVERIFY(!dataAt(*reader, "data"));
    QVERIFY(!reader->storedFile(0));
    QVERIFY(!reader->fileData(99));
    QVERIFY(!reader->storedFile(99));
    QVERIFY(!reader->openNested(99));
}

void TestRpfReader::readsSyntheticResource()
{
    const auto reader = openBytes(sampleArchive());
    QVERIFY(reader);
    const auto index = reader->archive().find("x.ytd");
    QVERIFY(index);
    QCOMPARE(reader->fileData(*index).value(), pages);
    const auto stored = reader->storedFile(*index);
    QVERIFY(stored);
    const auto header = parseRsc7Header(std::span(*stored).first(16));
    QVERIFY(header);
    QCOMPARE(header->systemFlags, 0x08000000u);
    QCOMPARE(header->graphicsFlags, 0x08000000u);
    QCOMPARE(std::vector<std::byte>(stored->begin() + 16, stored->end()), deflateRaw(pages));
}

void TestRpfReader::readsFromFileSource()
{
    QTemporaryDir directory;
    const std::filesystem::path file = std::filesystem::path(directory.path().toStdString()) / "sample.rpf";
    const auto bytes = sampleArchive();
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const auto source = openFileSource(file);
    QCOMPARE(source->size(), std::uint64_t(bytes.size()));
    QVERIFY(!source->read(bytes.size() - 1, 2));
    QVERIFY(!source->read(bytes.size() + 1, 0));
    const auto reader = RpfReader::open(source, "sample.rpf", nullptr);
    QVERIFY(reader);
    QCOMPARE(toText(dataAt(*reader, "data/a.txt").value()), std::string("hello"));
    const auto missing = RpfReader::open(openFileSource(file.parent_path() / "missing.rpf"), "missing.rpf", nullptr);
    QVERIFY(!missing);
}

void TestRpfReader::subSourceStaysInsideItsRange()
{
    const auto parent = memorySource(toVector("0123456789"));
    const auto sub = openSubSource(parent, 2, 4);
    QCOMPARE(sub->size(), std::uint64_t(4));
    QCOMPARE(toText(sub->read(1, 3).value()), std::string("345"));
    QVERIFY(!sub->read(1, 4));
    QVERIFY(!sub->read(5, 0));
    const auto outside = openSubSource(parent, 8, 4);
    QVERIFY(!outside->read(0, 4));
}

void TestRpfReader::readsNestedArchive()
{
    RpfBuilder inner;
    inner.addBinary("in.txt", toVector("inner"), false);
    RpfBuilder outer;
    outer.addBinary("sub/inner.rpf", inner.build(), false);
    outer.addBinary("sub/packed.rpf", inner.build(), true);
    outer.addBinary("sub/plain.txt", toVector("x"), false);
    const auto reader = openBytes(outer.build());
    QVERIFY(reader);
    const auto nested = reader->openNested(*reader->archive().find("sub/inner.rpf"));
    QVERIFY2(nested, nested ? "" : nested.error().message.c_str());
    QCOMPARE(toText(dataAt(*nested, "in.txt").value()), std::string("inner"));
    QVERIFY(!reader->openNested(*reader->archive().find("sub/packed.rpf")));
    QVERIFY(!reader->openNested(*reader->archive().find("sub/plain.txt")));
    QVERIFY(!reader->openNested(*reader->archive().find("sub")));
}

void TestRpfReader::rejectsDeepNesting()
{
    RpfBuilder innermost;
    innermost.addBinary("in.txt", toVector("deep"), false);
    std::vector<std::byte> bytes = innermost.build();
    for (int level = 0; level < 8; ++level) {
        RpfBuilder wrapper;
        wrapper.addBinary("n.rpf", std::move(bytes), false);
        bytes = wrapper.build();
    }
    auto reader = openBytes(std::move(bytes));
    QVERIFY(reader);
    for (int level = 2; level <= 8; ++level) {
        reader = reader->openNested(*reader->archive().find("n.rpf"));
        QVERIFY2(reader, qPrintable(QString::number(level)));
    }
    const auto ninth = reader->openNested(*reader->archive().find("n.rpf"));
    QVERIFY(!ninth);
    QCOMPARE(ninth.error().message, std::string("archive nesting too deep"));
}

void TestRpfReader::rejectsLyingUncompressedSize()
{
    auto bytes = sampleArchive();
    bytes[compressedFileEntry + 8] = std::byte { 0x87 };
    bytes[compressedFileEntry + 9] = std::byte { 0x13 };
    auto reader = openBytes(bytes);
    QVERIFY(reader);
    QCOMPARE(reader->archive().entries()[4].uncompressedSize, 4999u);
    QVERIFY(!dataAt(*reader, "data/b.bin"));

    bytes[compressedFileEntry + 8] = std::byte { 0x89 };
    reader = openBytes(bytes);
    QVERIFY(reader);
    QVERIFY(!dataAt(*reader, "data/b.bin"));

    for (int i = 0; i < 4; ++i)
        bytes[compressedFileEntry + 8 + i] = static_cast<std::byte>(i == 0 ? 0xF0 : 0xFF);
    reader = openBytes(bytes);
    QVERIFY(reader);
    const auto huge = dataAt(*reader, "data/b.bin");
    QVERIFY(!huge);
    QCOMPARE(huge.error().message, std::string("declared size exceeds the allocation limit"));
}

void TestRpfReader::rejectsLyingResource()
{
    auto bytes = sampleArchive();
    const std::size_t resourceEntry = 16 + 2 * 16;
    bytes[resourceEntry + 8] = std::byte { 1 };
    auto reader = openBytes(bytes);
    QVERIFY(reader);
    QVERIFY(!dataAt(*reader, "x.ytd"));

    bytes[resourceEntry + 8] = std::byte { 0xFF };
    bytes[resourceEntry + 9] = std::byte { 0xFF };
    bytes[resourceEntry + 10] = std::byte { 0xFF };
    bytes[resourceEntry + 11] = std::byte { 0x0F };
    reader = openBytes(bytes);
    QVERIFY(reader);
    const auto huge = dataAt(*reader, "x.ytd");
    QVERIFY(!huge);
    QCOMPARE(huge.error().message, std::string("declared size exceeds the allocation limit"));
}

void TestRpfReader::rejectsTruncatedData()
{
    const auto bytes = sampleArchive();
    const auto whole = openBytes(bytes);
    QVERIFY(whole);
    const RpfEntry &entry = whole->archive().entries()[4];
    const std::size_t cut = static_cast<std::size_t>(entry.offset) + entry.storedSize / 2;
    std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
    QVERIFY(!openBytes(truncated));
    const auto lying = RpfReader::open(memorySource(truncated, bytes.size()), "test.rpf", nullptr);
    QVERIFY(lying);
    QVERIFY(!dataAt(*lying, "data/b.bin"));
    QVERIFY(dataAt(*lying, "data/a.txt"));
}

void TestRpfReader::encryptedEntryWithoutKeysFails()
{
    auto bytes = sampleArchive();
    bytes[compressedFileEntry + 12] = std::byte { 1 };
    const auto reader = openBytes(bytes);
    QVERIFY(reader);
    const auto data = dataAt(*reader, "data/b.bin");
    QVERIFY(!data);
    QCOMPARE(data.error().message, std::string("entry is encrypted and no keys are loaded"));
}

void TestRpfReader::readsRealBinaryEntry()
{
    ARCHON_REQUIRE_REAL();
    const auto reader = openReal("update/update.rpf");
    QVERIFY2(reader, reader ? "" : reader.error().message.c_str());
    const auto data = dataAt(*reader, "common/data/dlclist.xml");
    QVERIFY2(data, data ? "" : data.error().message.c_str());
    QVERIFY(toText(*data).starts_with("<?xml"));
}

void TestRpfReader::readsRealResourceEntry()
{
    ARCHON_REQUIRE_REAL();
    const auto reader = openReal("x64a.rpf");
    QVERIFY2(reader, reader ? "" : reader.error().message.c_str());
    const auto &entries = reader->archive().entries();
    const auto first = std::ranges::find(entries, RpfEntryKind::Resource, &RpfEntry::kind);
    QVERIFY(first != entries.end());
    const auto index = static_cast<std::uint32_t>(first - entries.begin());
    const auto stored = reader->storedFile(index);
    QVERIFY2(stored, stored ? "" : stored.error().message.c_str());
    QCOMPARE(toText(std::span(*stored).first(4)), std::string("RSC7"));
    const auto data = reader->fileData(index);
    QVERIFY2(data, data ? "" : data.error().message.c_str());
    QCOMPARE(std::uint64_t(data->size()), rsc7SizeFromFlags(first->systemFlags) + rsc7SizeFromFlags(first->graphicsFlags));
}

void TestRpfReader::opensRealNestedArchive()
{
    ARCHON_REQUIRE_REAL();
    const auto reader = openReal("update/update.rpf");
    QVERIFY(reader);
    const auto &entries = reader->archive().entries();
    const auto first = std::ranges::find_if(entries, [](const RpfEntry &entry) {
        return entry.kind == RpfEntryKind::Binary && asciiLower(entry.name).ends_with(".rpf");
    });
    QVERIFY(first != entries.end());
    const auto nested = reader->openNested(static_cast<std::uint32_t>(first - entries.begin()));
    QVERIFY2(nested, nested ? "" : nested.error().message.c_str());
    QVERIFY(nested->archive().entries().size() >= 1);
}

QTEST_APPLESS_MAIN(TestRpfReader)
#include "tst_rpf_reader.moc"
