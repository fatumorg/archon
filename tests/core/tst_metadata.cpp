#include "core/io/ascii.h"
#include "core/meta/metadata_document.h"
#include "core/meta/name_table.h"
#include "core/rpf/rpf_reader.h"
#include "support/meta_samples.h"
#include "support/real_install.h"
#include "support/real_keys.h"
#include "support/test_bytes.h"

#include <QTemporaryDir>
#include <QTest>

using namespace archon;
using namespace archon::test;

class TestMetadata : public QObject {
    Q_OBJECT
private slots:
    void namesResolveBothHashings();
    void namesHarvestIdentifiersAndPersist();
    void psoReadsAsXmlAndSavesIdentically();
    void editingAValueChangesOnlyItsBytes();
    void addingAnItemGrowsTheArray();
    void misnamedFieldIsRefusedWithItsLine();
    void hostilePsoIsRefused();
    void rbfReadsAsXmlAndSavesIdentically();
    void textIsNotBinaryMetadata();
    void realFilesRoundTrip();
};

namespace {

const char *sampleXml = R"(<?xml version="1.0"?>
<csample>
  <fvalue value="1.5" />
  <modelname>adder</modelname>
  <items>
    <Item value="1" />
    <Item value="2" />
    <Item value="3" />
  </items>
  <label>hello</label>
</csample>
)";

std::string replaced(std::string text, const std::string &from, const std::string &to)
{
    text.replace(text.find(from), from.size(), to);
    return text;
}

}

void TestMetadata::namesResolveBothHashings()
{
    NameTable names;
    names.add("CHandlingData");
    const std::uint32_t lower = joaat("chandlingdata");
    QCOMPARE(names.name(lower), std::string("CHandlingData"));
    QCOMPARE(names.value(lower), std::string("CHandlingData"));
    QCOMPARE(names.name(joaat("CHandlingData")), std::string("CHandlingData"));
    QCOMPARE(names.value(joaat("CHandlingData")), std::string("hash_0E9464D1"));
    QCOMPARE(names.name(0x12345678), std::string("hash_12345678"));
    QCOMPARE(names.valueHash("hash_0000ABCD"), std::optional<std::uint32_t>(0xABCD));
    QCOMPARE(names.valueHash("CHandlingData"), std::optional<std::uint32_t>(lower));
    QCOMPARE(names.valueHash(""), std::optional<std::uint32_t>(0));
    QVERIFY(!names.valueHash("hash_xyz"));
}

void TestMetadata::namesHarvestIdentifiersAndPersist()
{
    NameTable names;
    names.harvest("<CVehicleModelInfo__InitDataList>\n  <modelName>adder</modelName> fMass=\"1\" 9bad _ok\n");
    for (const char *word : { "CVehicleModelInfo__InitDataList", "modelName", "adder", "fMass", "_ok" })
        QCOMPARE(names.value(joaat(asciiLower(word))), std::string(word));
    QCOMPARE(names.value(joaat("9bad")), std::string("hash_") + QString::number(joaat("9bad"), 16).toUpper().rightJustified(8, '0').toStdString());
    QTemporaryDir directory;
    const auto file = std::filesystem::path(directory.path().toStdString()) / "names.txt";
    QVERIFY(names.save(file));
    NameTable loaded;
    QVERIFY(loaded.load(file));
    QCOMPARE(loaded.value(joaat("adder")), std::string("adder"));
    QCOMPARE(loaded.size(), names.size());
}

void TestMetadata::psoReadsAsXmlAndSavesIdentically()
{
    const auto file = samplePso();
    const auto document = openMetadata(file, sampleNames());
    QVERIFY2(document, document ? "" : document.error().message.c_str());
    QCOMPARE(document->format, MetadataFormat::Pso);
    QCOMPARE(document->xml, std::string(sampleXml));
    QCOMPARE(document->readOnlyReason, std::string());
    const auto saved = saveMetadata(file, document->xml, sampleNames());
    QVERIFY(saved);
    QVERIFY(*saved == file);
}

void TestMetadata::editingAValueChangesOnlyItsBytes()
{
    const auto file = samplePso();
    const auto saved = saveMetadata(file, replaced(sampleXml, "value=\"1.5\"", "value=\"2.5\""), sampleNames());
    QVERIFY2(saved, saved ? "" : saved.error().message.c_str());
    QCOMPARE(saved->size(), file.size());
    std::vector<std::size_t> changed;
    for (std::size_t index = 0; index < file.size(); ++index) {
        if ((*saved)[index] != file[index])
            changed.push_back(index);
    }
    const auto value = std::ranges::search(file, std::vector<std::byte> { std::byte { 0x3F }, std::byte { 0xC0 }, std::byte { 0 }, std::byte { 0 } });
    const auto at = static_cast<std::size_t>(value.begin() - file.begin());
    QCOMPARE(changed, (std::vector<std::size_t> { at, at + 1 }));
    QVERIFY(std::ranges::equal(std::span(*saved).subspan(at, 4), std::vector<std::byte> { std::byte { 0x40 }, std::byte { 0x20 }, std::byte { 0 }, std::byte { 0 } }));
    const auto reopened = openMetadata(*saved, sampleNames());
    QVERIFY(reopened);
    QVERIFY(reopened->xml.find("<fvalue value=\"2.5\" />") != std::string::npos);
}

void TestMetadata::addingAnItemGrowsTheArray()
{
    const auto saved = saveMetadata(samplePso(), replaced(sampleXml, "<Item value=\"3\" />", "<Item value=\"3\" />\n    <Item value=\"4\" />"), sampleNames());
    QVERIFY2(saved, saved ? "" : saved.error().message.c_str());
    const auto reopened = openMetadata(*saved, sampleNames());
    QVERIFY(reopened);
    QCOMPARE(reopened->xml, replaced(sampleXml, "<Item value=\"3\" />", "<Item value=\"3\" />\n    <Item value=\"4\" />"));
    QCOMPARE(reopened->readOnlyReason, std::string());
}

void TestMetadata::misnamedFieldIsRefusedWithItsLine()
{
    const auto refused = saveMetadata(samplePso(), replaced(sampleXml, "<modelname>adder</modelname>", "<modelnam>adder</modelnam>"), sampleNames());
    QVERIFY(!refused);
    QCOMPARE(refused.error().message, std::string("<modelnam> on line 4: expected <modelname> here"));
    const auto missing = saveMetadata(samplePso(), replaced(sampleXml, "  <label>hello</label>\n", ""), sampleNames());
    QVERIFY(!missing);
    QCOMPARE(missing.error().message, std::string("<csample> on line 2: <label> is missing"));
    QVERIFY(!saveMetadata(samplePso(), replaced(sampleXml, "value=\"2\"", "value=\"two\""), sampleNames()));
    QVERIFY(!saveMetadata(samplePso(), "<csample>", sampleNames()));
}

void TestMetadata::hostilePsoIsRefused()
{
    const auto file = samplePso();
    const auto names = sampleNames();
    std::vector<std::byte> badPointer = file;
    const auto root = std::ranges::search(badPointer, std::vector<std::byte> { std::byte { 0x3F }, std::byte { 0xC0 }, std::byte { 0 }, std::byte { 0 } });
    const std::size_t pointer = static_cast<std::size_t>(root.begin() - badPointer.begin()) + 8;
    badPointer[pointer + 3] = std::byte { 9 };
    const auto missingBlock = openMetadata(badPointer, names);
    QVERIFY(!missingBlock);
    QCOMPARE(missingBlock.error().message, std::string("a pointer names block 9, which does not exist"));
    std::vector<std::byte> bigCount = file;
    bigCount[pointer + 8] = std::byte { 0xFF };
    QVERIFY(!openMetadata(bigCount, names));
    for (std::size_t size : { std::size_t(4), std::size_t(10), file.size() / 2, file.size() - 1 })
        QVERIFY(!openMetadata(std::span(file).first(size), names));
    std::vector<std::byte> badLength = file;
    badLength[7] = std::byte { 0xFF };
    QVERIFY(!openMetadata(badLength, names));
}

void TestMetadata::rbfReadsAsXmlAndSavesIdentically()
{
    const auto file = sampleRbf();
    const auto document = openMetadata(file, NameTable());
    QVERIFY2(document, document ? "" : document.error().message.c_str());
    QCOMPARE(document->format, MetadataFormat::Rbf);
    QCOMPARE(document->xml, std::string(R"(<?xml version="1.0"?>
<Root>
  <Item type="CDef">
    <count value="-2" />
    <radius value="1.5" />
    <flag value="true" />
    <min x="1.0" y="2.0" z="3.0" />
    <Name>adder</Name>
  </Item>
</Root>
)"));
    QCOMPARE(document->readOnlyReason, std::string());
    const auto edited = saveMetadata(file, replaced(document->xml, "adder", "zentorno"), NameTable());
    QVERIFY(edited);
    QVERIFY(openMetadata(*edited, NameTable())->xml.find("<Name>zentorno</Name>") != std::string::npos);
    QVERIFY(!openMetadata(std::span(file).first(file.size() - 2), NameTable()));
}

void TestMetadata::textIsNotBinaryMetadata()
{
    QVERIFY(!isBinaryMetadata(toVector("<CVehicleModelInfo />")));
    QVERIFY(isBinaryMetadata(samplePso()));
    QVERIFY(isBinaryMetadata(sampleRbf()));
}

void TestMetadata::realFilesRoundTrip()
{
    ARCHON_REQUIRE_REAL();
    const Keys *keys = realKeys();
    QVERIFY(keys);
    const auto reader = RpfReader::open(openFileSource(*realGameDir() / "x64a.rpf"), "x64a.rpf", keys);
    QVERIFY(reader);
    NameTable names;
    std::size_t exact = 0;
    std::size_t readOnly = 0;
    std::vector<std::string> failed;
    const std::function<void(const RpfReader &, const std::string &)> walk = [&](const RpfReader &archive, const std::string &prefix) {
        const auto &entries = archive.archive().entries();
        for (std::uint32_t index = 0; index < entries.size(); ++index) {
            const std::string name = asciiLower(entries[index].name);
            if (entries[index].kind == RpfEntryKind::Binary && name.ends_with(".rpf")) {
                if (const auto nested = archive.openNested(index))
                    walk(*nested, prefix + archive.archive().path(index) + "/");
                continue;
            }
            if (!name.ends_with(".ymt") && !name.ends_with(".ymap") && !name.ends_with(".ytyp") && !name.ends_with(".ymf"))
                continue;
            const auto file = archive.storedFile(index);
            if (!file || !isBinaryMetadata(*file))
                continue;
            const auto document = openMetadata(*file, names);
            if (!document)
                failed.push_back(prefix + archive.archive().path(index) + ": " + document.error().message);
            else if (document->readOnlyReason.empty())
                ++exact;
            else
                ++readOnly;
        }
    };
    walk(*reader, "x64a.rpf/");
    for (const std::string &failure : failed)
        qWarning("%s", failure.c_str());
    qInfo("x64a.rpf: %zu metadata files saved byte-exact, %zu read-only", exact, readOnly);
    QVERIFY(failed.empty());
    QVERIFY(exact > 400);
    QVERIFY(readOnly * 50 < exact);
}

QTEST_APPLESS_MAIN(TestMetadata)
#include "tst_metadata.moc"
