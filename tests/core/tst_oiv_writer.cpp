#include "core/io/zip_writer.h"
#include "core/oiv/oiv_installer.h"
#include "core/oiv/oiv_writer.h"
#include "core/rpf/rpf_reader.h"
#include "support/browse_fixture.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestOivWriter : public QObject {
    Q_OBJECT
private slots:
    void init();
    void zipWriterRoundTrips();
    void assemblyHasThe22Shape();
    void targetsSplitIntoArchives();
    void specialCharactersSurvive();
    void duplicateFileNamesSurvive();
    void invalidDraftsAreRefused();
    void writtenPackageInstalls();

private:
    std::filesystem::path source(const std::string &relative, std::string_view content)
    {
        const std::filesystem::path file = pathOf(*directory_) / "sources" / relative;
        writeFile(file, content);
        return file;
    }
    PackageDraft draft()
    {
        return { "My Mod", 1, 2, "Beta", "Me", "https://example.com/", "Short.", "History", "1.2: things.", "Free.", 0xFF23366A, false, 0xFF3B5998, {}, {} };
    }

    std::unique_ptr<QTemporaryDir> directory_;
};

void TestOivWriter::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
}

void TestOivWriter::zipWriterRoundTrips()
{
    ZipWriter writer;
    QVERIFY(writer.add("a.txt", toVector("alpha")));
    QVERIFY(writer.addFile("folder/b.bin", source("b.bin", "beta")));
    QVERIFY(!writer.addFile("missing.bin", pathOf(*directory_) / "nope"));
    const auto bytes = writer.finish();
    QVERIFY2(bytes, bytes ? "" : bytes.error().message.c_str());
    const auto zip = ZipArchive::open(*bytes);
    QVERIFY(zip);
    QCOMPARE(zip->names(), (std::vector<std::string> { "a.txt", "folder/b.bin" }));
    QCOMPARE(toText(*zip->read("folder/b.bin", 100)), std::string("beta"));
}

void TestOivWriter::assemblyHasThe22Shape()
{
    PackageDraft package = draft();
    package.items.push_back({ PackageItem::Kind::Add, source("ScriptMod.asi", "asi"), "ScriptMod.asi" });
    const auto xml = assemblyXml(package, "{12345678-90AB-CDEF-1234-567890ABCDEF}");
    QVERIFY2(xml, xml ? "" : xml.error().message.c_str());
    QVERIFY(xml->starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
    QVERIFY(xml->find("<package version=\"2.2\" id=\"{12345678-90AB-CDEF-1234-567890ABCDEF}\" target=\"Five\">") != std::string::npos);
    QVERIFY(xml->find("<headerBackground useBlackTextColor=\"False\">$FF23366A</headerBackground>") != std::string::npos);
    QVERIFY(xml->find("<iconBackground>$FF3B5998</iconBackground>") != std::string::npos);
    QVERIFY(xml->find("<major>1</major>") != std::string::npos);
    QVERIFY(xml->find("<add source=\"1/ScriptMod.asi\">ScriptMod.asi</add>") != std::string::npos);
}

void TestOivWriter::targetsSplitIntoArchives()
{
    PackageDraft package = draft();
    package.items = {
        { PackageItem::Kind::Add, source("x.meta", "meta"), "update\\update.rpf\\common\\data\\x.meta" },
        { PackageItem::Kind::Add, source("adder.yft", "yft"), "x64e.rpf/levels/gta5/vehicles.rpf/adder.yft" },
        { PackageItem::Kind::Add, source("adder.ytd", "ytd"), "x64e.rpf/levels/gta5/vehicles.rpf/adder.ytd" },
        { PackageItem::Kind::Delete, {}, "x64e.rpf/levels/gta5/vehicles.rpf/old.ytd" },
        { PackageItem::Kind::Add, source("ScriptMod.asi", "asi"), "ScriptMod.asi" },
    };
    const auto bytes = writeOivPackage(package);
    QVERIFY2(bytes, bytes ? "" : bytes.error().message.c_str());
    const auto zip = ZipArchive::open(*bytes);
    const auto read = readOivPackage(*zip);
    QVERIFY2(read, read ? "" : read.error().message.c_str());
    const auto &commands = read->contents[0].commands;
    QCOMPARE(commands.size(), std::size_t(3));
    QCOMPARE(commands[0].kind, OivCommand::Kind::Archive);
    QCOMPARE(commands[0].path, std::string("update/update.rpf"));
    QCOMPARE(commands[0].children[0].path, std::string("common/data/x.meta"));
    QCOMPARE(commands[1].path, std::string("x64e.rpf"));
    QCOMPARE(commands[1].children.size(), std::size_t(1));
    const OivCommand &vehicles = commands[1].children[0];
    QCOMPARE(vehicles.path, std::string("levels/gta5/vehicles.rpf"));
    QCOMPARE(vehicles.children.size(), std::size_t(3));
    QCOMPARE(vehicles.children[2].kind, OivCommand::Kind::Delete);
    QCOMPARE(commands[2].kind, OivCommand::Kind::Add);
    QCOMPARE(toText(*oivSource(*zip, *read, vehicles.children[1].source)), std::string("ytd"));
}

void TestOivWriter::specialCharactersSurvive()
{
    PackageDraft package = draft();
    package.name = "Cars & <Bikes>";
    package.description = "Line one\nLine \"two\" ]]> end & more";
    package.items.push_back({ PackageItem::Kind::Add, source("a.txt", "a"), "a.txt" });
    const auto zip = ZipArchive::open(*writeOivPackage(package));
    const auto read = readOivPackage(*zip);
    QVERIFY2(read, read ? "" : read.error().message.c_str());
    QCOMPARE(read->name, package.name);
    QCOMPARE(read->description, package.description);
    QCOMPARE(read->largeDescriptionTitle, std::string("History"));
    QCOMPARE(read->major, 1);
    QCOMPARE(read->minor, 2);
    QCOMPARE(read->tag, std::string("Beta"));
}

void TestOivWriter::duplicateFileNamesSurvive()
{
    PackageDraft package = draft();
    package.items = { { PackageItem::Kind::Add, source("one/same.txt", "first"), "a/same.txt" }, { PackageItem::Kind::Add, source("two/same.txt", "second"), "b/same.txt" } };
    const auto zip = ZipArchive::open(*writeOivPackage(package));
    const auto read = readOivPackage(*zip);
    QCOMPARE(toText(*oivSource(*zip, *read, read->contents[0].commands[0].source)), std::string("first"));
    QCOMPARE(toText(*oivSource(*zip, *read, read->contents[0].commands[1].source)), std::string("second"));
}

void TestOivWriter::invalidDraftsAreRefused()
{
    PackageDraft empty = draft();
    QCOMPARE(writeOivPackage(empty).error().message, std::string("The package has no content."));
    PackageDraft noName = draft();
    noName.name.clear();
    noName.items.push_back({ PackageItem::Kind::Add, source("a.txt", "a"), "a.txt" });
    QCOMPARE(writeOivPackage(noName).error().message, std::string("The package needs a name."));
    PackageDraft hostile = draft();
    hostile.items.push_back({ PackageItem::Kind::Add, source("a.txt", "a"), "..\\outside.txt" });
    QVERIFY(!writeOivPackage(hostile));
    PackageDraft archiveTarget = draft();
    archiveTarget.items.push_back({ PackageItem::Kind::Delete, {}, "update/update.rpf" });
    QVERIFY(writeOivPackage(archiveTarget));
    PackageDraft missing = draft();
    missing.items.push_back({ PackageItem::Kind::Add, pathOf(*directory_) / "nope.txt", "a.txt" });
    QVERIFY(!writeOivPackage(missing));
}

void TestOivWriter::writtenPackageInstalls()
{
    const std::filesystem::path root = pathOf(*directory_) / "game";
    writeBrowseFixture(root);
    PackageDraft package = draft();
    package.items = {
        { PackageItem::Kind::Add, source("n.txt", "nested file"), "common.rpf/inner.rpf/deep/n.txt" },
        { PackageItem::Kind::Delete, {}, "common.rpf/x.ytd" },
        { PackageItem::Kind::Add, source("mod.asi", "asi"), "mod.asi" },
    };
    const auto zip = ZipArchive::open(*writeOivPackage(package));
    const auto read = readOivPackage(*zip);
    QVERIFY(read);
    const auto installed = installOivPackage(*zip, *read, 0, InstallTarget::Game, root, nullptr, pathOf(*directory_) / "store");
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    QCOMPARE(toText(readFile(root / "mod.asi")), std::string("asi"));
    auto reader = RpfReader::open(openFileSource(root / "common.rpf"), "common.rpf", nullptr);
    QVERIFY(!reader->archive().find("x.ytd"));
    const auto inner = reader->openNested(*reader->archive().find("inner.rpf"));
    QCOMPARE(toText(*inner->fileData(*inner->archive().find("deep/n.txt"))), std::string("nested file"));
}

QTEST_APPLESS_MAIN(TestOivWriter)
#include "tst_oiv_writer.moc"
