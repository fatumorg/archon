#include "core/oiv/oiv_package.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"
#include "support/zip_builder.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTest>

using namespace archon;
using namespace archon::test;

class TestOivPackage : public QObject {
    Q_OBJECT
private slots:
    void readsVersion22Fields();
    void readsCommandTree();
    void versions20And21UseLastAsDefault();
    void reads11AndKeepsOnlyFive();
    void refuses11WithoutFive();
    void refusesWrongTargetAndArchiveType();
    void hostilePathsRefused();
    void normalizesPaths();
    void sourceLookupIgnoresCaseAndSeparators();
    void dtdIsNotExpanded();
    void readsPublicSamples();
};

namespace {

const std::string colors = R"(<colors><headerBackground useBlackTextColor="True">$FF23366A</headerBackground><iconBackground>$FF3b5998</iconBackground></colors>)";

std::string package22(const std::string &content, const std::string &version = "2.2", const std::string &target = "Five")
{
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<package version=")" + version + R"(" id="{BDA5F91A-EC81-45ED-A101-480E9F5928D4}" target=")" + target + R"(">
<metadata>
  <name>Test Package</name>
  <version><major>2</major><minor>3</minor><tag>EXAMPLE</tag></version>
  <author><displayName>Somebody</displayName><actionLink>http://example.com/</actionLink></author>
  <description footerLink="http://example.com/"><![CDATA[Short text.]]></description>
  <largeDescription displayName="Version history"><![CDATA[Long text.]]></largeDescription>
  <licence><![CDATA[Licence text.]]></licence>
</metadata>
)" + colors + "<content>" + content + "</content></package>";
}

Result<OivPackage> read(const std::vector<std::pair<std::string, std::string>> &members, ZipArchive *keep = nullptr)
{
    auto zip = ZipArchive::open(makeZip(members));
    if (!zip)
        return std::unexpected(zip.error());
    auto package = readOivPackage(*zip);
    if (keep != nullptr)
        *keep = std::move(*zip);
    return package;
}

}


void TestOivPackage::readsVersion22Fields()
{
    const auto package = read({ { "assembly.xml", package22("") }, { "icon.png", "PNGDATA" } });
    QVERIFY2(package, package ? "" : package.error().message.c_str());
    QCOMPARE(package->version, std::string("2.2"));
    QCOMPARE(package->name, std::string("Test Package"));
    QCOMPARE(package->major, 2);
    QCOMPARE(package->minor, 3);
    QCOMPARE(package->tag, std::string("EXAMPLE"));
    QCOMPARE(package->author, std::string("Somebody"));
    QCOMPARE(package->authorLink, std::string("http://example.com/"));
    QCOMPARE(package->description, std::string("Short text."));
    QCOMPARE(package->largeDescriptionTitle, std::string("Version history"));
    QCOMPARE(package->largeDescription, std::string("Long text."));
    QCOMPARE(package->licence, std::string("Licence text."));
    QCOMPARE(package->headerColor, std::uint32_t(0xFF23366A));
    QVERIFY(package->blackHeaderText);
    QCOMPARE(package->iconColor, std::uint32_t(0xFF3B5998));
    QCOMPARE(toText(package->icon), std::string("PNGDATA"));
    QCOMPARE(package->contents.size(), std::size_t(1));
    QCOMPARE(package->contents[0].name, std::string("Install"));
}

void TestOivPackage::readsCommandTree()
{
    const std::string content = R"(
<add source="ScriptMod.asi">ScriptMod.asi</add>
<delete>data\Old.rpf</delete>
<archive path="update\update.rpf" createIfNotExist="False" type="RPF7">
  <add source="water.xml">data\levels\gta5\water.xml</add>
  <archive path="x64\dlcpacks\a\dlc.rpf" createIfNotExist="True" type="RPF7">
    <delete>x.ytd</delete>
  </archive>
  <defragmentation archive="x64\dlcpacks\a\dlc.rpf"/>
  <text path="common\data\info.dat" createIfNotExist="True">
    <add>added</add>
    <insert where="Before" line="Line 1" condition="Equal">first</insert>
    <replace line="Line 4" condition="StartWith">new</replace>
    <delete condition="Mask">#*</delete>
  </text>
  <xml path="common\data\dlclist.xml">
    <add append="Before" xpath="/a/b"><Item>one</Item><Item>two</Item></add>
    <replace xpath="/a/c"><c>3</c></replace>
    <remove xpath="/a/d"/>
  </xml>
  <pso path="x64\data\carcols.ymt"><remove xpath="/x"/></pso>
</archive>
<defragmentation archive="x64a.rpf"/>)";
    const auto package = read({ { "assembly.xml", package22(content) } });
    QVERIFY2(package, package ? "" : package.error().message.c_str());
    const auto &commands = package->contents[0].commands;
    QCOMPARE(commands.size(), std::size_t(4));
    QCOMPARE(commands[0].kind, OivCommand::Kind::Add);
    QCOMPARE(commands[0].source, std::string("ScriptMod.asi"));
    QCOMPARE(commands[0].path, std::string("ScriptMod.asi"));
    QCOMPARE(commands[1].kind, OivCommand::Kind::Delete);
    QCOMPARE(commands[1].path, std::string("data/Old.rpf"));
    QCOMPARE(commands[3].kind, OivCommand::Kind::Defragment);
    QCOMPARE(commands[3].path, std::string("x64a.rpf"));
    const OivCommand &archive = commands[2];
    QCOMPARE(archive.kind, OivCommand::Kind::Archive);
    QCOMPARE(archive.path, std::string("update/update.rpf"));
    QVERIFY(!archive.createIfNotExist);
    QCOMPARE(archive.children.size(), std::size_t(6));
    QCOMPARE(archive.children[1].kind, OivCommand::Kind::Archive);
    QVERIFY(archive.children[1].createIfNotExist);
    QCOMPARE(archive.children[1].children[0].path, std::string("x.ytd"));
    QCOMPARE(archive.children[2].kind, OivCommand::Kind::Defragment);
    const OivCommand &text = archive.children[3];
    QCOMPARE(text.kind, OivCommand::Kind::Text);
    QVERIFY(text.createIfNotExist);
    QCOMPARE(text.text.size(), std::size_t(4));
    QCOMPARE(text.text[0].kind, OivText::Kind::Add);
    QCOMPARE(text.text[0].value, std::string("added"));
    QCOMPARE(text.text[1].kind, OivText::Kind::Insert);
    QVERIFY(!text.text[1].after);
    QCOMPARE(text.text[1].line, std::string("Line 1"));
    QCOMPARE(text.text[1].condition, OivText::Condition::Equal);
    QCOMPARE(text.text[2].condition, OivText::Condition::StartWith);
    QCOMPARE(text.text[3].kind, OivText::Kind::Delete);
    QCOMPARE(text.text[3].line, std::string("#*"));
    QCOMPARE(text.text[3].condition, OivText::Condition::Mask);
    const OivCommand &xml = archive.children[4];
    QCOMPARE(xml.kind, OivCommand::Kind::Xml);
    QCOMPARE(xml.xml.size(), std::size_t(3));
    QCOMPARE(xml.xml[0].append, OivXml::Append::Before);
    QCOMPARE(xml.xml[0].xpath, std::string("/a/b"));
    QCOMPARE(xml.xml[0].fragment, std::string("<Item>one</Item><Item>two</Item>"));
    QCOMPARE(xml.xml[1].kind, OivXml::Kind::Replace);
    QCOMPARE(xml.xml[2].kind, OivXml::Kind::Remove);
    QCOMPARE(archive.children[5].kind, OivCommand::Kind::Pso);
}

void TestOivPackage::versions20And21UseLastAsDefault()
{
    for (const char *version : { "2.0", "2.1" }) {
        const auto package = read({ { "assembly.xml", package22(R"(<xml path="a.xml"><add xpath="/a"><b/></add></xml>)", version) } });
        QVERIFY2(package, package ? "" : package.error().message.c_str());
        QCOMPARE(package->contents[0].commands[0].xml[0].append, OivXml::Append::Last);
    }
    QVERIFY(!read({ { "assembly.xml", package22("", "3.0") } }));
}

void TestOivPackage::reads11AndKeepsOnlyFive()
{
    const std::string assembly = R"(<?xml version="1.0" encoding="UTF-8"?>
<package version="1.1">
  <metadata><name>Old</name><author>Writer</author><target><game>IV</game><game>Five</game></target><description>Short</description></metadata>
  <content gameID="IV" name="For IV" description="x"><add source="content\a.txt">a.txt</add></content>
  <content gameID="Five" name="Install" description="Main"><add source="content\a.txt">temp\a.txt</add>
    <replace source="content\b.txt">b.txt</replace>
    <archive:open path="x.rpf" createIfNotExist="True" type="RPF7"><add source="content\a.txt">a.txt</add></archive:open>
    <archive:rebuild path="x.rpf"/>
    <text:open path="t.txt" createIfNotExist="False"><add>z</add></text:open>
  </content>
  <content gameID="Five" name="Alternative" description="Other"><delete>temp\a.txt</delete></content>
</package>)";
    const auto package = read({ { "assembly.xml", assembly } });
    QVERIFY2(package, package ? "" : package.error().message.c_str());
    QCOMPARE(package->version, std::string("1.1"));
    QCOMPARE(package->author, std::string("Writer"));
    QCOMPARE(package->contents.size(), std::size_t(2));
    QCOMPARE(package->contents[0].name, std::string("Install"));
    QCOMPARE(package->contents[1].name, std::string("Alternative"));
    const auto &commands = package->contents[0].commands;
    QCOMPARE(commands.size(), std::size_t(5));
    QCOMPARE(commands[0].source, std::string("content/a.txt"));
    QCOMPARE(commands[1].kind, OivCommand::Kind::Add);
    QCOMPARE(commands[2].kind, OivCommand::Kind::Archive);
    QCOMPARE(commands[3].kind, OivCommand::Kind::Defragment);
    QCOMPARE(commands[4].kind, OivCommand::Kind::Text);
}

void TestOivPackage::refuses11WithoutFive()
{
    const std::string assembly = R"(<package version="1.1"><metadata><name>Old</name><author>W</author><target><game>IV</game><game>EFLC</game></target><description>d</description></metadata>
<content gameID="IV" name="Install" description="x"/><content gameID="EFLC" name="Install" description="x"/></package>)";
    const auto package = read({ { "assembly.xml", assembly } });
    QVERIFY(!package);
    QCOMPARE(package.error().message, std::string("This package is not for GTA V (it targets IV, EFLC)."));
}

void TestOivPackage::refusesWrongTargetAndArchiveType()
{
    const auto wrongGame = read({ { "assembly.xml", package22("", "2.2", "IV") } });
    QVERIFY(!wrongGame);
    QCOMPARE(wrongGame.error().message, std::string("This package is not for GTA V (it targets IV)."));
    QVERIFY(!read({ { "assembly.xml", package22(R"(<archive path="a.img" createIfNotExist="False" type="IMG3"/>)") } }));
    QVERIFY(!read({ { "assembly.xml", "<package" } }));
    QVERIFY(!read({ { "readme.txt", "no assembly" } }));
}

void TestOivPackage::hostilePathsRefused()
{
    for (const char *path : { "..\\outside.dll", "a\\..\\..\\b", "C:\\Windows\\x.dll", "a/./../../b" }) {
        const auto package = read({ { "assembly.xml", package22(std::string(R"(<add source="a.txt">)") + path + "</add>") } });
        QVERIFY2(!package, path);
    }
    QVERIFY(!read({ { "assembly.xml", package22(R"(<add source="..\..\secret">a.txt</add>)") } }));
    QVERIFY(!normalizedOivPath(std::string("a\0b", 3)));
    QVERIFY(!normalizedOivPath(""));
}

void TestOivPackage::normalizesPaths()
{
    QCOMPARE(*normalizedOivPath("update\\x64//dlcpacks\\a\\"), std::string("update/x64/dlcpacks/a"));
    QCOMPARE(*normalizedOivPath("/Folder/SubFolder/T.txt"), std::string("Folder/SubFolder/T.txt"));
    QCOMPARE(*normalizedOivPath("./a/./b"), std::string("a/b"));
}

void TestOivPackage::sourceLookupIgnoresCaseAndSeparators()
{
    ZipArchive zip = ZipArchive::open(makeZip({ { "x", "y" } })).value();
    const auto package = read({ { "assembly.xml", package22(R"(<add source="Adder\Vehicles\ADDER.yft">a.yft</add>)") }, { "content/adder/vehicles/adder.yft", "model" } }, &zip);
    QVERIFY(package);
    const auto source = oivSourceName(zip, *package, package->contents[0].commands[0].source);
    QVERIFY2(source, source ? "" : source.error().message.c_str());
    QCOMPARE(toText(*zip.read(*source, 1024)), std::string("model"));
    QVERIFY(!oivSourceName(zip, *package, "missing.bin"));
}

void TestOivPackage::dtdIsNotExpanded()
{
    const std::string assembly = R"(<?xml version="1.0"?>
<!DOCTYPE package [<!ENTITY a "aaaaaaaaaa"><!ENTITY b "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;"><!ENTITY c "&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;">]>
<package version="2.2" id="{X}" target="Five"><metadata><name>&c;</name><version><major>1</major><minor>0</minor></version><author><displayName>x</displayName></author><description>d</description></metadata>)" + colors + "<content/></package>";
    const auto package = read({ { "assembly.xml", assembly } });
    if (package)
        QVERIFY(package->name.size() < 100);
}

void TestOivPackage::readsPublicSamples()
{
    const QByteArray folder = qgetenv("ARCHON_OIV_SAMPLES");
    if (folder.isEmpty())
        QSKIP("ARCHON_OIV_SAMPLES is not set");
    const std::filesystem::path samples(folder.toStdString());
    const auto package21 = ZipArchive::open(readFile(samples / "Package21.zip"));
    QVERIFY(package21);
    const auto read21 = readOivPackage(*package21);
    QVERIFY2(read21, read21 ? "" : read21.error().message.c_str());
    QCOMPARE(read21->version, std::string("2.1"));
    QCOMPARE(read21->contents[0].commands.size(), std::size_t(2));
    QVERIFY(oivSourceName(*package21, *read21, "dlclist.xml"));

    const auto package11 = ZipArchive::open(readFile(samples / "Package11.zip"));
    QVERIFY(package11);
    const auto read11 = readOivPackage(*package11);
    QVERIFY(!read11);
    QVERIFY(read11.error().message.starts_with("This package is not for GTA V"));

    std::vector<std::pair<std::string, std::string>> members;
    const QString base = QString::fromStdString((samples / "tutorial" / "Install").string());
    QDirIterator it(base, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        QFile opened(file);
        QVERIFY(opened.open(QIODevice::ReadOnly));
        members.emplace_back(QDir(base).relativeFilePath(file).toStdString(), opened.readAll().toStdString());
    }
    const auto tutorialZip = ZipArchive::open(makeZip(members));
    QVERIFY(tutorialZip);
    const auto tutorial = readOivPackage(*tutorialZip);
    QVERIFY2(tutorial, tutorial ? "" : tutorial.error().message.c_str());
    QCOMPARE(tutorial->contents[0].commands.size(), std::size_t(4));
    QVERIFY(oivSourceName(*tutorialZip, *tutorial, "adder/vehicles/adder.yft"));
    QVERIFY(!oivSourceName(*tutorialZip, *tutorial, "r8ppi/dlc.rpf"));
}

QTEST_APPLESS_MAIN(TestOivPackage)
#include "tst_oiv_package.moc"
