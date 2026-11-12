#include "core/meta/metadata_document.h"
#include "core/oiv/oiv_installer.h"
#include "core/rpf/rpf_reader.h"
#include "core/safety/transaction.h"
#include "support/browse_fixture.h"
#include "support/meta_samples.h"
#include "support/real_install.h"
#include "support/real_keys.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"
#include "support/zip_builder.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestOivInstaller : public QObject {
    Q_OBJECT
private slots:
    void init();
    void looseCommandsOnDisk();
    void archiveCommandsGoToMods();
    void createsArchivesAndDefragments();
    void failureRollsEverythingBack();
    void resolvesCaseAndSeparators();
    void existingModsCopyIsReused();
    void oneHistoryEntryThatRollsBack();
    void psoEditsGoThroughXml();
    void installsOfficialExample();
    void installsTutorialOnCopies();
    void psoEditsARealFile();

private:
    Result<void> install(const std::string &content, std::vector<std::pair<std::string, std::string>> files, InstallTarget target)
    {
        files.emplace_back("assembly.xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<package version="2.2" id="{1}" target="Five"><metadata><name>Test Package</name><version><major>2</major><minor>3</minor></version>
<author><displayName>T</displayName></author><description>d</description></metadata>
<colors><headerBackground useBlackTextColor="False">$FF23366A</headerBackground><iconBackground>$FF3b5998</iconBackground></colors>
<content>)" + content + "</content></package>");
        auto zip = ZipArchive::open(makeZip(files));
        if (!zip)
            return std::unexpected(zip.error());
        const auto package = readOivPackage(*zip);
        if (!package)
            return std::unexpected(package.error());
        return installOivPackage(*zip, *package, 0, target, root_, nullptr, store_);
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::filesystem::path store_;
};

namespace {

std::string entry(const std::filesystem::path &file, std::vector<std::string> nested, std::string_view path, const Keys *keys = nullptr)
{
    auto reader = RpfReader::open(openFileSource(file), file.filename().string(), keys);
    if (!reader)
        return "<" + reader.error().message + ">";
    for (const std::string &level : nested) {
        const auto index = reader->archive().find(level);
        if (!index)
            return "<missing " + level + ">";
        auto next = reader->openNested(*index);
        if (!next)
            return "<" + next.error().message + ">";
        reader = std::move(next);
    }
    const auto index = reader->archive().find(path);
    if (!index)
        return "<missing>";
    const auto data = reader->fileData(*index);
    return data ? toText(*data) : "<unreadable>";
}

}

void TestOivInstaller::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    store_ = pathOf(*directory_) / "store";
    writeBrowseFixture(root_);
    writeFile(root_ / "settings.dat", "keep=1\r\nold=2\r\n");
}

void TestOivInstaller::looseCommandsOnDisk()
{
    const auto installed = install(R"(
<add source="ScriptMod.asi">ScriptMod.asi</add>
<add source="sub\conf.ini">scripts\conf.ini</add>
<delete>GTA5.exe</delete>
<delete>not_there.txt</delete>
<text path="settings.dat" createIfNotExist="False"><replace line="old=" condition="StartWith">old=3</replace></text>
<text path="new\list.txt" createIfNotExist="True"><add>first</add></text>)",
        { { "content/ScriptMod.asi", "asi" }, { "content/sub/conf.ini", "ini" } }, InstallTarget::Game);
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    QCOMPARE(toText(readFile(root_ / "ScriptMod.asi")), std::string("asi"));
    QCOMPARE(toText(readFile(root_ / "scripts" / "conf.ini")), std::string("ini"));
    QVERIFY(!std::filesystem::exists(root_ / "GTA5.exe"));
    QCOMPARE(toText(readFile(root_ / "settings.dat")), std::string("keep=1\r\nold=3\r\n"));
    QCOMPARE(toText(readFile(root_ / "new" / "list.txt")), std::string("first"));
}

void TestOivInstaller::archiveCommandsGoToMods()
{
    const auto common = readFile(root_ / "common.rpf");
    const auto update = readFile(root_ / "update" / "update.rpf");
    const auto installed = install(R"(
<archive path="common.rpf" createIfNotExist="False" type="RPF7">
  <add source="n.txt">new\n.txt</add>
  <delete>x.ytd</delete>
  <archive path="inner.rpf" createIfNotExist="False" type="RPF7"><add source="n.txt">deep\d.txt</add></archive>
  <text path="data\a.txt" createIfNotExist="False"><add>world</add></text>
</archive>
<archive path="update\update.rpf" createIfNotExist="False" type="RPF7">
  <xml path="common\data\dlclist.xml"><add xpath="/SMandatoryPacksData"><Paths><Item>dlcpacks:\mine\</Item></Paths></add></xml>
</archive>
<add source="dlc.rpf">update\x64\dlcpacks\mine\dlc.rpf</add>)",
        { { "content/n.txt", "new file" }, { "content/dlc.rpf", "archive" } }, InstallTarget::Mods);
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    QCOMPARE(readFile(root_ / "common.rpf"), common);
    QCOMPARE(readFile(root_ / "update" / "update.rpf"), update);
    const std::filesystem::path mods = root_ / "mods";
    QCOMPARE(entry(mods / "common.rpf", {}, "new/n.txt"), std::string("new file"));
    QCOMPARE(entry(mods / "common.rpf", {}, "x.ytd"), std::string("<missing>"));
    QCOMPARE(entry(mods / "common.rpf", { "inner.rpf" }, "deep/d.txt"), std::string("new file"));
    QCOMPARE(entry(mods / "common.rpf", { "inner.rpf" }, "deep/c.txt"), std::string("deep content"));
    QCOMPARE(entry(mods / "common.rpf", {}, "data/a.txt"), std::string("hello\r\nworld"));
    QVERIFY(entry(mods / "update" / "update.rpf", {}, "common/data/dlclist.xml").find("<Item>dlcpacks:\\mine\\</Item>") != std::string::npos);
    QCOMPARE(toText(readFile(mods / "update" / "x64" / "dlcpacks" / "mine" / "dlc.rpf")), std::string("archive"));
}

void TestOivInstaller::createsArchivesAndDefragments()
{
    const auto installed = install(R"(
<archive path="x64_OIV.rpf" createIfNotExist="True" type="RPF7">
  <add source="t.txt">TestTextFile.txt</add>
  <archive path="inner\x64a.rpf" createIfNotExist="True" type="RPF7">
    <add source="t.txt">Package\Installer\Test\TestTextFile.txt</add>
  </archive>
  <defragmentation archive="inner\x64a.rpf"/>
</archive>
<defragmentation archive="x64_OIV.rpf"/>)",
        { { "content/t.txt", "text" } }, InstallTarget::Game);
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    QCOMPARE(entry(root_ / "x64_OIV.rpf", {}, "TestTextFile.txt"), std::string("text"));
    QCOMPARE(entry(root_ / "x64_OIV.rpf", { "inner/x64a.rpf" }, "Package/Installer/Test/TestTextFile.txt"), std::string("text"));
    QCOMPARE(std::filesystem::file_size(root_ / "x64_OIV.rpf") % 512, std::uintmax_t(0));
    QVERIFY(!install(R"(<archive path="absent.rpf" createIfNotExist="False" type="RPF7"><add source="t.txt">t.txt</add></archive>)", { { "content/t.txt", "t" } }, InstallTarget::Game));
}

void TestOivInstaller::failureRollsEverythingBack()
{
    const auto common = readFile(root_ / "common.rpf");
    const auto installed = install(R"(
<add source="a.asi">a.asi</add>
<archive path="common.rpf" createIfNotExist="False" type="RPF7"><add source="a.asi">a.asi</add></archive>
<archive path="common.rpf" createIfNotExist="False" type="RPF7"><add source="missing.bin">m.bin</add></archive>)",
        { { "content/a.asi", "asi" } }, InstallTarget::Game);
    QVERIFY(!installed);
    QVERIFY(installed.error().message.find("missing.bin") != std::string::npos);
    QVERIFY(!std::filesystem::exists(root_ / "a.asi"));
    QCOMPARE(readFile(root_ / "common.rpf"), common);
    QVERIFY(listTransactions(store_).empty());
    QVERIFY(!install(R"(<archive path="common.rpf" createIfNotExist="False" type="RPF7"><xml path="data\a.txt"><remove xpath="/x"/></xml></archive>)", {}, InstallTarget::Game));
    QCOMPARE(readFile(root_ / "common.rpf"), common);
}

void TestOivInstaller::resolvesCaseAndSeparators()
{
    const auto installed = install(R"(<archive path="Update/UPDATE.rpf" createIfNotExist="False" type="RPF7">
  <xml path="Common\Data\DLCLIST.xml"><add xpath="SMandatoryPacksData"><Item>x</Item></add></xml></archive>)",
        {}, InstallTarget::Game);
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    QVERIFY(!std::filesystem::exists(root_ / "Update"));
    QVERIFY(entry(root_ / "update" / "update.rpf", {}, "common/data/dlclist.xml").find("<Item>x</Item>") != std::string::npos);
}

void TestOivInstaller::existingModsCopyIsReused()
{
    std::filesystem::create_directories(root_ / "Mods");
    std::filesystem::copy_file(root_ / "common.rpf", root_ / "Mods" / "common.rpf");
    QVERIFY(install(R"(<archive path="common.rpf" createIfNotExist="False" type="RPF7"><add source="one.txt">one.txt</add></archive>)", { { "content/one.txt", "1" } }, InstallTarget::Mods));
    QVERIFY(install(R"(<archive path="common.rpf" createIfNotExist="False" type="RPF7"><add source="two.txt">two.txt</add></archive>)", { { "content/two.txt", "2" } }, InstallTarget::Mods));
    QCOMPARE(entry(root_ / "Mods" / "common.rpf", {}, "one.txt"), std::string("1"));
    QCOMPARE(entry(root_ / "Mods" / "common.rpf", {}, "two.txt"), std::string("2"));
    QVERIFY(!std::filesystem::exists(root_ / "mods"));
}

void TestOivInstaller::oneHistoryEntryThatRollsBack()
{
    const auto common = readFile(root_ / "common.rpf");
    QVERIFY(install(R"(<add source="a.asi">a.asi</add><archive path="common.rpf" createIfNotExist="False" type="RPF7"><add source="a.asi">a.asi</add></archive>
<defragmentation archive="common.rpf"/>)", { { "content/a.asi", "asi" } }, InstallTarget::Mods));
    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(1));
    QCOMPARE(history.front().description, std::string("Install package Test Package 2.3"));
    QVERIFY(rollbackTransaction(store_, history.front().id));
    QVERIFY(!std::filesystem::exists(root_ / "a.asi"));
    QVERIFY(!std::filesystem::exists(root_ / "mods"));
    QCOMPARE(readFile(root_ / "common.rpf"), common);
}

void TestOivInstaller::psoEditsGoThroughXml()
{
    std::filesystem::create_directories(root_ / "data");
    writeFile(root_ / "data" / "x.ymt", samplePso());
    std::filesystem::create_directories(store_);
    QVERIFY(sampleNames().save(nameTableFile(store_)));
    const auto installed = install(R"(<pso path="data\x.ymt"><remove xpath="/csample/items/Item[3]"/></pso>)", {}, InstallTarget::Game);
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    const auto reopened = openMetadata(readFile(root_ / "data" / "x.ymt"), sampleNames());
    QVERIFY2(reopened, reopened ? "" : reopened.error().message.c_str());
    QVERIFY(reopened->xml.find("<Item value=\"2\" />") != std::string::npos);
    QVERIFY(reopened->xml.find("<Item value=\"3\" />") == std::string::npos);
    const auto missing = install(R"(<pso path="data\none.ymt"><remove xpath="/csample"/></pso>)", {}, InstallTarget::Game);
    QVERIFY(!missing);
}

void TestOivInstaller::installsOfficialExample()
{
    const QByteArray folder = qgetenv("ARCHON_OIV_SAMPLES");
    if (folder.isEmpty())
        QSKIP("ARCHON_OIV_SAMPLES is not set");
    const auto zip = ZipArchive::open(readFile(std::filesystem::path(folder.toStdString()) / "Package21.zip"));
    QVERIFY(zip);
    const auto package = readOivPackage(*zip);
    QVERIFY(package);
    const auto installed = installOivPackage(*zip, *package, 0, InstallTarget::Game, root_, nullptr, store_);
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    const std::string text = entry(root_ / "x64_OIV.rpf", {}, "TestTextFile.txt");
    QVERIFY(text.starts_with("This is first line\r\nLine 1\r\nLine 2\r\nTHIS IS NEW LINE\r\nLine 5\r\nThis is last line\r\n"));
    QVERIFY(text.find('#') == std::string::npos);
    const std::string dlclist = entry(root_ / "x64_OIV.rpf", {}, "dlclist.xml");
    QVERIFY(dlclist.find("<item>platform:\\dlcPacks\\xmTestLineOnTop\\</item>") < dlclist.find("platform:\\dlcPacks\\mpBeach\\"));
    QVERIFY(dlclist.find("<item>dlcpacks:\\spLibertyV\\</item>") != std::string::npos);
    QVERIFY(dlclist.find("mpReplacedLine") != std::string::npos);
    QCOMPARE(entry(root_ / "x64_OIV.rpf", { "inner/x64a.rpf" }, "Package/Installer/Test/TestTextFile.txt").substr(0, 6), std::string("Line 1"));
}

void TestOivInstaller::installsTutorialOnCopies()
{
    ARCHON_REQUIRE_REAL();
    const QByteArray folder = qgetenv("ARCHON_OIV_SAMPLES");
    if (folder.isEmpty())
        QSKIP("ARCHON_OIV_SAMPLES is not set");
    const Keys *keys = realKeys();
    QVERIFY(keys);
    std::filesystem::create_directories(scratchDirectory());
    QTemporaryDir scratch(QString::fromStdString((scratchDirectory() / "oiv-XXXXXX").string()));
    const std::filesystem::path root = pathOf(scratch) / "game";
    const std::filesystem::path store = pathOf(scratch) / "store";
    std::filesystem::create_directories(root / "update");
    std::filesystem::copy_file(*realGameDir() / "x64e.rpf", root / "x64e.rpf");
    std::filesystem::copy_file(*realGameDir() / "update" / "update.rpf", root / "update" / "update.rpf");

    const std::filesystem::path base = std::filesystem::path(folder.toStdString()) / "tutorial" / "Install";
    std::string assembly = toText(readFile(base / "assembly.xml"));
    std::vector<std::pair<std::string, std::string>> members;
    for (const char *name : { "content/adder/vehicles/adder.yft", "content/adder/vehicles/adder.ytd", "content/adder/vehicles/adder_hi.yft" })
        members.emplace_back(name, toText(readFile(base / name)));

    const auto shared = std::make_shared<const Keys>(*keys);
    {
        members.emplace_back("assembly.xml", assembly);
        const auto zip = ZipArchive::open(makeZip(members));
        const auto package = readOivPackage(*zip);
        QVERIFY(package);
        const auto x64e = readFile(root / "x64e.rpf");
        const auto failed = installOivPackage(*zip, *package, 0, InstallTarget::Game, root, shared, store);
        QVERIFY(!failed);
        QVERIFY(failed.error().message.find("r8ppi/dlc.rpf") != std::string::npos);
        QCOMPARE(readFile(root / "x64e.rpf"), x64e);
        members.pop_back();
    }

    const std::string missing = R"(<add source="r8ppi/dlc.rpf">update/x64/dlcpacks/r8ppi/dlc.rpf</add>)";
    assembly.erase(assembly.find(missing), missing.size());
    members.emplace_back("assembly.xml", assembly);
    const auto zip = ZipArchive::open(makeZip(members));
    const auto package = readOivPackage(*zip);
    QVERIFY(package);
    const auto installed = installOivPackage(*zip, *package, 0, InstallTarget::Mods, root, shared, store);
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    {
        auto reader = RpfReader::open(openFileSource(root / "mods" / "x64e.rpf"), "x64e.rpf", keys);
        QVERIFY(reader);
        auto vehicles = reader->openNested(*reader->archive().find("levels/gta5/vehicles.rpf"));
        QVERIFY2(vehicles, vehicles ? "" : vehicles.error().message.c_str());
        for (const char *name : { "adder.yft", "adder.ytd", "adder_hi.yft" }) {
            const auto stored = vehicles->storedFile(*vehicles->archive().find(name));
            QVERIFY(stored);
            QCOMPARE(*stored, readFile(base / "content" / "adder" / "vehicles" / name));
        }
        auto original = RpfReader::open(openFileSource(*realGameDir() / "x64e.rpf"), "x64e.rpf", keys);
        QVERIFY(original);
        const auto originalVehicles = original->openNested(*original->archive().find("levels/gta5/vehicles.rpf"));
        QVERIFY(originalVehicles);
        QCOMPARE(vehicles->archive().entries().size(), originalVehicles->archive().entries().size());
    }
    const std::string dlclist = entry(root / "mods" / "update" / "update.rpf", {}, "common/data/dlclist.xml", keys);
    QVERIFY(dlclist.find("<item>dlcpacks:\\r8ppi\\</item>") != std::string::npos);
    const std::string handling = entry(root / "mods" / "update" / "update.rpf", {}, "common/data/handling.meta", keys);
    QVERIFY(handling.find("<handlingName>ADDER</handlingName>\n\t\t\t<fMass value=\"1800.000000\" />") != std::string::npos);
    const auto history = listTransactions(store);
    QCOMPARE(history.size(), std::size_t(1));
    QVERIFY(rollbackTransaction(store, history.front().id));
    QVERIFY(!std::filesystem::exists(root / "mods"));
}

void TestOivInstaller::psoEditsARealFile()
{
    ARCHON_REQUIRE_REAL();
    const Keys *keys = realKeys();
    QVERIFY(keys);
    const auto reader = RpfReader::open(openFileSource(*realGameDir() / "x64a.rpf"), "x64a.rpf", keys);
    QVERIFY(reader);
    const auto index = reader->archive().find("data/playerspecialabilities.ymt");
    QVERIFY(index);
    const auto original = reader->storedFile(*index);
    QVERIFY(original);
    std::filesystem::create_directories(root_ / "data");
    writeFile(root_ / "data" / "playerspecialabilities.ymt", *original);
    const auto before = openMetadata(*original, NameTable());
    QVERIFY(before && before->readOnlyReason.empty());
    QVERIFY(before->xml.find("value=\"0.325\"") != std::string::npos);
    const auto installed = install(R"(<pso path="data\playerspecialabilities.ymt"><remove xpath="/hash_A042ED3C/hash_6208614E/Item[1]"/></pso>)", {}, InstallTarget::Game);
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    QVERIFY(std::filesystem::exists(nameTableFile(store_)));
    const auto after = openMetadata(readFile(root_ / "data" / "playerspecialabilities.ymt"), NameTable());
    QVERIFY2(after, after ? "" : after.error().message.c_str());
    QVERIFY(after->readOnlyReason.empty());
    QVERIFY(after->xml.find("value=\"0.325\"") == std::string::npos);
    QVERIFY(after->xml.find("value=\"0.8\"") != std::string::npos);
}

QTEST_APPLESS_MAIN(TestOivInstaller)
#include "tst_oiv_installer.moc"
