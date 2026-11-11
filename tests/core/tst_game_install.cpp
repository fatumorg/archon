#include "core/game/game_install.h"
#include "support/real_install.h"
#include "support/real_keys.h"
#include "support/rpf_builder.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QTest>

#include <algorithm>
#include <memory>

using namespace archon;
using namespace archon::test;

namespace {

std::string libraryFolders(const std::filesystem::path &first, const std::filesystem::path &second)
{
    return "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + first.string() + "\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"271590\"\t\t\"1\"\n\t\t}\n\t}\n"
        + "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + second.string() + "\"\n\t}\n}\n";
}

std::string manifest(std::string_view installDirectory)
{
    return "\"AppState\"\n{\n\t\"appid\"\t\t\"271590\"\n\t\"installdir\"\t\t\"" + std::string(installDirectory) + "\"\n}\n";
}

}

class TestGameInstall : public QObject {
    Q_OBJECT
private slots:
    void detectsEdition();
    void findsSteamInstalls();
    void listsArchivesSkippingMods();
    void readsDlcListFromSyntheticInstall();
    void realInstall();
};

void TestGameInstall::detectsEdition()
{
    QTemporaryDir directory;
    const auto root = pathOf(directory);
    writeFile(root / "legacy" / "gta5.EXE");
    writeFile(root / "enhanced" / "GTA5_Enhanced.exe");
    std::filesystem::create_directories(root / "empty");

    const auto legacy = openGameInstall(root / "legacy");
    QVERIFY(legacy);
    QCOMPARE(legacy->edition, GameEdition::Legacy);
    QCOMPARE(legacy->executable, root / "legacy" / "gta5.EXE");
    QCOMPARE(legacy->root, root / "legacy");

    const auto enhanced = openGameInstall(root / "enhanced");
    QVERIFY(enhanced);
    QCOMPARE(enhanced->edition, GameEdition::Enhanced);

    const auto empty = openGameInstall(root / "empty");
    QVERIFY(!empty);
    QCOMPARE(empty.error().message, "no GTA V executable in " + (root / "empty").string());
    QVERIFY(!openGameInstall(root / "missing"));
}

void TestGameInstall::findsSteamInstalls()
{
    QTemporaryDir directory;
    const auto home = pathOf(directory);
    const auto first = home / "library one";
    const auto second = home / "library two";
    writeFile(home / ".local/share/Steam/steamapps/libraryfolders.vdf", libraryFolders(first, second));
    writeFile(home / ".steam/steam/steamapps/libraryfolders.vdf", libraryFolders(first, second));
    writeFile(first / "steamapps/appmanifest_271590.acf", manifest("Grand Theft Auto V"));
    writeFile(first / "steamapps/common/Grand Theft Auto V/GTA5.exe");
    writeFile(second / "steamapps/appmanifest_3240220.acf", manifest("Grand Theft Auto V Enhanced"));

    const auto installs = findSteamInstalls(home);
    QCOMPARE(installs.size(), std::size_t(1));
    QCOMPARE(installs[0].edition, GameEdition::Legacy);
    QCOMPARE(installs[0].root, std::filesystem::canonical(first / "steamapps/common/Grand Theft Auto V"));

    writeFile(second / "steamapps/common/Grand Theft Auto V Enhanced/GTA5_Enhanced.exe");
    QCOMPARE(findSteamInstalls(home).size(), std::size_t(2));

    QTemporaryDir broken;
    writeFile(pathOf(broken) / ".local/share/Steam/steamapps/libraryfolders.vdf", "\"libraryfolders\" { \"0\" { \"path\" \"/nowhere");
    QVERIFY(findSteamInstalls(pathOf(broken)).empty());
    writeFile(pathOf(broken) / ".local/share/Steam/steamapps/libraryfolders.vdf", std::string("\0\xFF{{{\"\"\"\\", 10));
    QVERIFY(findSteamInstalls(pathOf(broken)).empty());
    QVERIFY(findSteamInstalls(pathOf(broken) / "missing").empty());
}

void TestGameInstall::listsArchivesSkippingMods()
{
    QTemporaryDir directory;
    const auto root = pathOf(directory);
    writeFile(root / "GTA5.exe");
    writeFile(root / "x64a.rpf");
    writeFile(root / "update/update.RPF");
    writeFile(root / "mods/x64a.rpf");
    writeFile(root / "readme.txt");
    std::filesystem::create_directories(root / "folder.rpf");
    const auto install = openGameInstall(root);
    QVERIFY(install);
    const auto archives = listArchives(*install);
    QCOMPARE(archives, (std::vector<std::filesystem::path> { "update/update.RPF", "x64a.rpf" }));
}

void TestGameInstall::readsDlcListFromSyntheticInstall()
{
    QTemporaryDir directory;
    const auto root = pathOf(directory);
    writeFile(root / "GTA5.exe");
    RpfBuilder builder;
    builder.addBinary("common/data/dlclist.xml", toVector("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<SMandatoryPacksData>\n\t<Paths>\n"
                                                          "\t\t<Item>dlcpacks:/mpBeach/</Item>\n\t\t<Item>dlcpacks:\\patchday1ng\\</Item>\n\t\t<Item>other:/x/</Item>\n"
                                                          "\t</Paths>\n</SMandatoryPacksData>\n"),
        true);
    writeFile(root / "Update" / "update.rpf", builder.build());
    const auto install = openGameInstall(root);
    QVERIFY(install);
    const auto keys = std::make_unique<Keys>();
    const auto names = readDlcList(*install, *keys);
    QVERIFY2(names, names ? "" : names.error().message.c_str());
    QCOMPARE(*names, (std::vector<std::string> { "mpbeach", "patchday1ng" }));

    RpfBuilder malformed;
    malformed.addBinary("common/data/dlclist.xml", toVector("<Paths><Item>dlcpacks:/a/</Item>"), false);
    writeFile(root / "Update" / "update.rpf", malformed.build());
    QVERIFY(!readDlcList(*install, *keys));

    std::filesystem::remove(root / "Update" / "update.rpf");
    QVERIFY(!readDlcList(*install, *keys));
}

void TestGameInstall::realInstall()
{
    ARCHON_REQUIRE_REAL();
    const auto install = openGameInstall(*realGameDir());
    QVERIFY(install);
    QCOMPARE(install->edition, GameEdition::Legacy);
    const auto archives = listArchives(*install);
    for (const char *expected : { "common.rpf", "x64a.rpf", "update/update.rpf" })
        QVERIFY2(std::ranges::find(archives, std::filesystem::path(expected)) != archives.end(), expected);
    QVERIFY(std::ranges::count_if(archives, [](const std::filesystem::path &path) { return path.filename() == "dlc.rpf"; }) >= 90);
    const Keys *keys = realKeys();
    QVERIFY(keys);
    const auto names = readDlcList(*install, *keys);
    QVERIFY2(names, names ? "" : names.error().message.c_str());
    QVERIFY(names->size() >= 50);
    QVERIFY(std::ranges::none_of(*names, &std::string::empty));
}

QTEST_APPLESS_MAIN(TestGameInstall)
#include "tst_game_install.moc"
