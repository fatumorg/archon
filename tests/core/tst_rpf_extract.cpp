#include "core/rpf/rpf_extract.h"
#include "support/memory_source.h"
#include "support/real_install.h"
#include "support/real_keys.h"
#include "support/rpf_builder.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QTest>

#include <algorithm>
#include <set>

using namespace archon;
using namespace archon::test;

namespace {

RpfReader sampleReader()
{
    RpfBuilder builder;
    builder.addBinary("data/a.txt", toVector("hello"), false);
    builder.addBinary("data/b.bin", std::vector<std::byte>(5000, std::byte { 'z' }), true);
    builder.addResource("x.ytd", std::vector<std::byte>(0x400, std::byte { 7 }), 0x08000000, 0x08000000);
    return RpfReader::open(memorySource(builder.build()), "test.rpf", nullptr).value();
}

std::string contentOf(const std::filesystem::path &file)
{
    std::ifstream stream(file, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

std::set<std::string> filesUnder(const std::filesystem::path &root)
{
    std::set<std::string> result;
    for (const auto &item : std::filesystem::recursive_directory_iterator(root))
        result.insert(item.path().lexically_relative(root).generic_string());
    return result;
}

void rename(std::vector<std::byte> &archive, std::string_view from, std::string_view to)
{
    const auto fromBytes = asBytes(from);
    const auto found = std::ranges::search(archive, fromBytes);
    QVERIFY(!found.empty() && to.size() == from.size());
    std::ranges::copy(asBytes(to), found.begin());
}

}

class TestRpfExtract : public QObject {
    Q_OBJECT
private slots:
    void extractsFileAndResource();
    void refusesOverwrite();
    void isSafeEntryNameCases();
    void hostileNamesStayInside();
    void extractsTree();
    void extractTreeRejectsFiles();
    void extractsRealDirectory();
};

void TestRpfExtract::extractsFileAndResource()
{
    QTemporaryDir directory;
    const auto out = pathOf(directory) / "new" / "folder";
    const RpfReader reader = sampleReader();
    const auto text = extractEntry(reader, *reader.archive().find("data/a.txt"), out);
    QVERIFY2(text, text ? "" : text.error().message.c_str());
    QCOMPARE(*text, out / "a.txt");
    QCOMPARE(contentOf(out / "a.txt"), std::string("hello"));
    const auto resource = extractEntry(reader, *reader.archive().find("x.ytd"), out);
    QVERIFY(resource);
    QCOMPARE(contentOf(out / "x.ytd").substr(0, 4), std::string("RSC7"));
    QVERIFY(!extractEntry(reader, *reader.archive().find("data"), out));
    QVERIFY(!extractEntry(reader, 99, out));
    QCOMPARE(filesUnder(out), (std::set<std::string> { "a.txt", "x.ytd" }));
}

void TestRpfExtract::refusesOverwrite()
{
    QTemporaryDir directory;
    const auto out = pathOf(directory);
    const RpfReader reader = sampleReader();
    const auto index = *reader.archive().find("data/a.txt");
    QVERIFY(extractEntry(reader, index, out));
    writeFile(out / "a.txt", "changed");
    const auto second = extractEntry(reader, index, out);
    QVERIFY(!second);
    QCOMPARE(second.error().message, std::string("file already exists"));
    QCOMPARE(contentOf(out / "a.txt"), std::string("changed"));

    std::filesystem::remove(out / "a.txt");
    std::filesystem::create_symlink(out / "elsewhere.txt", out / "a.txt");
    QVERIFY(!extractEntry(reader, index, out));
    QVERIFY(!std::filesystem::exists(out / "elsewhere.txt"));
}

void TestRpfExtract::isSafeEntryNameCases()
{
    QVERIFY(isSafeEntryName("a.txt"));
    QVERIFY(isSafeEntryName("a b..txt"));
    QVERIFY(!isSafeEntryName(""));
    QVERIFY(!isSafeEntryName("."));
    QVERIFY(!isSafeEntryName(".."));
    QVERIFY(!isSafeEntryName("a/b"));
    QVERIFY(!isSafeEntryName("a\\b"));
    QVERIFY(!isSafeEntryName(std::string("a\0b", 3)));
    QVERIFY(!isSafeEntryName(" a"));
    QVERIFY(!isSafeEntryName("a "));
}

void TestRpfExtract::hostileNamesStayInside()
{
    RpfBuilder builder;
    builder.addBinary("aa", toVector("one"), false);
    builder.addBinary("0123456789ab", toVector("two"), false);
    builder.addBinary("dd/inside.txt", toVector("three"), false);
    builder.addBinary("good.txt", toVector("four"), false);
    auto bytes = builder.build();
    rename(bytes, std::string_view("aa\0", 3), std::string_view("..\0", 3));
    rename(bytes, "0123456789ab", "a/../../evil");
    rename(bytes, std::string_view("dd\0", 3), std::string_view("..\0", 3));
    const auto reader = RpfReader::open(memorySource(bytes), "test.rpf", nullptr);
    QVERIFY2(reader, reader ? "" : reader.error().message.c_str());

    QTemporaryDir directory;
    const auto base = pathOf(directory);
    const auto summary = extractTree(*reader, 0, base / "out");
    QVERIFY(summary);
    QCOMPARE(summary->failures.size(), std::size_t(3));
    QCOMPARE(summary->files, std::uint64_t(1));
    QCOMPARE(filesUnder(base), (std::set<std::string> { "out", "out/good.txt" }));
    for (std::uint32_t index = 1; index < reader->archive().entries().size(); ++index) {
        if (reader->archive().entries()[index].kind == RpfEntryKind::Binary && reader->archive().entries()[index].name != "good.txt"
            && reader->archive().entries()[index].name != "inside.txt")
            QVERIFY(!extractEntry(*reader, index, base / "out"));
    }
    QCOMPARE(filesUnder(base), (std::set<std::string> { "out", "out/good.txt" }));
}

void TestRpfExtract::extractsTree()
{
    QTemporaryDir directory;
    const auto out = pathOf(directory) / "out";
    const RpfReader reader = sampleReader();
    const auto summary = extractTree(reader, 0, out);
    QVERIFY(summary);
    QVERIFY(summary->failures.empty());
    QCOMPARE(summary->files, std::uint64_t(3));
    QCOMPARE(filesUnder(out), (std::set<std::string> { "data", "data/a.txt", "data/b.bin", "x.ytd" }));
    QCOMPARE(summary->bytes, std::filesystem::file_size(out / "data/a.txt") + std::filesystem::file_size(out / "data/b.bin") + std::filesystem::file_size(out / "x.ytd"));
    QCOMPARE(std::filesystem::file_size(out / "data/b.bin"), std::uintmax_t(5000));

    const auto again = extractTree(reader, 0, out);
    QVERIFY(again);
    QCOMPARE(again->files, std::uint64_t(0));
    QCOMPARE(again->failures.size(), std::size_t(3));
    QVERIFY(again->failures[0].message.ends_with("file already exists"));

    const auto sub = extractTree(reader, *reader.archive().find("data"), pathOf(directory) / "sub");
    QVERIFY(sub);
    QCOMPARE(filesUnder(pathOf(directory) / "sub"), (std::set<std::string> { "a.txt", "b.bin" }));
}

void TestRpfExtract::extractTreeRejectsFiles()
{
    QTemporaryDir directory;
    const RpfReader reader = sampleReader();
    QVERIFY(!extractTree(reader, *reader.archive().find("x.ytd"), pathOf(directory)));
    QVERIFY(!extractTree(reader, 99, pathOf(directory)));
}

void TestRpfExtract::extractsRealDirectory()
{
    ARCHON_REQUIRE_REAL();
    const auto reader = RpfReader::open(openFileSource(*realGameDir() / "common.rpf"), "common.rpf", realKeys());
    QVERIFY2(reader, reader ? "" : reader.error().message.c_str());
    const auto data = reader->archive().find("data");
    QVERIFY(data);
    QTemporaryDir directory;
    const auto summary = extractTree(*reader, *data, pathOf(directory));
    QVERIFY(summary);
    QVERIFY2(summary->failures.empty(), summary->failures.empty() ? "" : summary->failures[0].message.c_str());
    QVERIFY(summary->files >= 100);
}

QTEST_APPLESS_MAIN(TestRpfExtract)
#include "tst_rpf_extract.moc"
