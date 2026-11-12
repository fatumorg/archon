#include "core/asi/asi_manager.h"
#include "core/safety/transaction.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QTest>

#include <algorithm>

using namespace archon;
using namespace archon::test;

namespace {

std::span<const std::byte> bytesOf(const std::string &text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

std::optional<std::filesystem::path> downloads()
{
    const QByteArray value = qgetenv("ARCHON_ASI_DOWNLOADS");
    if (value.isEmpty())
        return std::nullopt;
    return std::filesystem::path(value.toStdString());
}

}

class TestAsiManager : public QObject {
    Q_OBJECT
private slots:
    void init();
    void pinnedContentRefusesUnknownBytes();
    void pinnedContentAcceptsRealArchiveAndMember();
    void installLoaderBacksUpExisting();
    void enhancedUsesXinput();
    void modsLoaderCreatesModsAndCountsOpenIvAsi();
    void uninstallModsLoaderRemovesEveryKnownLoader();
    void pluginsListedFromRootScriptsPlugins();
    void disableRenamesAndEnableRestores();
    void addPluginRefusesNonAsi();
    void everyActionIsOneHistoryEntry();

private:
    AsiManager manager(GameEdition edition = GameEdition::Legacy) const
    {
        return AsiManager(root_, edition, store_);
    }
    std::filesystem::path source(const std::string &name, std::string_view content)
    {
        const std::filesystem::path file = pathOf(*directory_) / "sources" / name;
        writeFile(file, content);
        return file;
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::filesystem::path store_;
};

void TestAsiManager::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    store_ = pathOf(*directory_) / "store";
    writeFile(root_ / "GTA5.exe", "exe");
}

void TestAsiManager::pinnedContentRefusesUnknownBytes()
{
    const auto refused = pinnedContent(asiLoaderDownload(), bytesOf("not the loader"));
    QVERIFY(!refused);
    QCOMPARE(refused.error().message, std::string("ASI Loader does not match the pinned SHA-256"));
    QCOMPARE(std::string(asiLoaderDownload().member), std::string("dinput8.dll"));
    QCOMPARE(std::string(modsLoaderDownload().member), std::string("RageOpenV.asi"));
    QVERIFY(std::string(asiLoaderDownload().url).starts_with("https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.2/"));
    QVERIFY(std::string(modsLoaderDownload().url).starts_with("https://github.com/Chiheb-Bacha/RageOpenV/releases/download/v1.0/"));
}

void TestAsiManager::pinnedContentAcceptsRealArchiveAndMember()
{
    const auto folder = downloads();
    if (!folder)
        QSKIP("ARCHON_ASI_DOWNLOADS is not set");
    const auto zip = readFile(*folder / "Ultimate-ASI-Loader-NoPDB_x64.zip");
    const auto fromZip = pinnedContent(asiLoaderDownload(), zip);
    QVERIFY2(fromZip, fromZip ? "" : fromZip.error().message.c_str());
    QCOMPARE(fromZip->size(), std::size_t(2435816));
    const auto direct = pinnedContent(asiLoaderDownload(), *fromZip);
    QVERIFY(direct);
    QCOMPARE(*direct, *fromZip);
    auto damaged = zip;
    damaged[damaged.size() / 2] ^= std::byte { 1 };
    QVERIFY(!pinnedContent(asiLoaderDownload(), damaged));
    const auto mods = pinnedContent(modsLoaderDownload(), readFile(*folder / "RageOpenV.zip"));
    QVERIFY2(mods, mods ? "" : mods.error().message.c_str());
    QCOMPARE(mods->size(), std::size_t(134144));
}

void TestAsiManager::installLoaderBacksUpExisting()
{
    writeFile(root_ / "dinput8.dll", "scripthook loader");
    QVERIFY(!manager().state().loaderPinned);
    QVERIFY(manager().state().loaderInstalled);
    const auto installed = manager().installLoader(bytesOf("new loader"));
    QVERIFY2(installed, installed ? "" : installed.error().message.c_str());
    QCOMPARE(toText(readFile(root_ / "dinput8.dll")), std::string("new loader"));
    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(1));
    QCOMPARE(history.front().description, std::string("Install ASI Loader"));
    QVERIFY(rollbackTransaction(store_, history.front().id));
    QCOMPARE(toText(readFile(root_ / "dinput8.dll")), std::string("scripthook loader"));
    QVERIFY(manager().uninstallLoader());
    QVERIFY(!std::filesystem::exists(root_ / "dinput8.dll"));
    QVERIFY(!manager().state().loaderInstalled);
}

void TestAsiManager::enhancedUsesXinput()
{
    QCOMPARE(std::string(loaderFileName(GameEdition::Legacy)), std::string("dinput8.dll"));
    QCOMPARE(std::string(loaderFileName(GameEdition::Enhanced)), std::string("xinput1_4.dll"));
    QVERIFY(manager(GameEdition::Enhanced).installLoader(bytesOf("loader")));
    QVERIFY(std::filesystem::exists(root_ / "xinput1_4.dll"));
    QVERIFY(!std::filesystem::exists(root_ / "dinput8.dll"));
    QVERIFY(manager(GameEdition::Enhanced).state().loaderInstalled);
}

void TestAsiManager::modsLoaderCreatesModsAndCountsOpenIvAsi()
{
    QCOMPARE(manager().state().modsLoader, std::string());
    QVERIFY(manager().installModsLoader(bytesOf("rage")));
    QVERIFY(std::filesystem::is_directory(root_ / "mods"));
    QCOMPARE(toText(readFile(root_ / "RageOpenV.asi")), std::string("rage"));
    QCOMPARE(manager().state().modsLoader, std::string("RageOpenV.asi"));
    std::filesystem::remove(root_ / "RageOpenV.asi");
    writeFile(root_ / "openiv.ASI", "openiv");
    QCOMPARE(manager().state().modsLoader, std::string("openiv.ASI"));
    QVERIFY(manager().state().plugins.empty());
}

void TestAsiManager::uninstallModsLoaderRemovesEveryKnownLoader()
{
    writeFile(root_ / "OpenIV.asi", "openiv");
    writeFile(root_ / "OpenRPF.asi", "openrpf");
    writeFile(root_ / "RageOpenV.asi", "rage");
    writeFile(root_ / "other.asi", "other");
    QVERIFY(manager().uninstallModsLoader());
    QVERIFY(!std::filesystem::exists(root_ / "OpenIV.asi"));
    QVERIFY(!std::filesystem::exists(root_ / "OpenRPF.asi"));
    QVERIFY(!std::filesystem::exists(root_ / "RageOpenV.asi"));
    QVERIFY(std::filesystem::exists(root_ / "other.asi"));
    QVERIFY(!manager().uninstallModsLoader());
}

void TestAsiManager::pluginsListedFromRootScriptsPlugins()
{
    writeFile(root_ / "Trainer.ASI", "1");
    writeFile(root_ / "scripts" / "menu.asi", "22");
    writeFile(root_ / "plugins" / "off.asi.disabled", "333");
    writeFile(root_ / "scripts" / "notes.txt", "x");
    writeFile(root_ / "mods" / "hidden.asi", "x");
    writeFile(root_ / "RageOpenV.asi", "rage");
    const auto plugins = manager().state().plugins;
    QCOMPARE(plugins.size(), std::size_t(3));
    QCOMPARE(plugins[0].path, std::string("plugins/off.asi.disabled"));
    QVERIFY(!plugins[0].enabled);
    QCOMPARE(plugins[0].size, std::uint64_t(3));
    QCOMPARE(plugins[1].path, std::string("scripts/menu.asi"));
    QVERIFY(plugins[1].enabled);
    QCOMPARE(plugins[2].path, std::string("Trainer.ASI"));
}

void TestAsiManager::disableRenamesAndEnableRestores()
{
    writeFile(root_ / "scripts" / "menu.asi", "menu");
    QVERIFY(manager().setPluginEnabled("scripts/menu.asi", false));
    QVERIFY(!std::filesystem::exists(root_ / "scripts" / "menu.asi"));
    QCOMPARE(toText(readFile(root_ / "scripts" / "menu.asi.disabled")), std::string("menu"));
    QVERIFY(!manager().setPluginEnabled("scripts/menu.asi.disabled", false));
    QVERIFY(manager().setPluginEnabled("scripts/menu.asi.disabled", true));
    QCOMPARE(toText(readFile(root_ / "scripts" / "menu.asi")), std::string("menu"));
    QVERIFY(!std::filesystem::exists(root_ / "scripts" / "menu.asi.disabled"));
    QVERIFY(manager().removePlugin("scripts/menu.asi"));
    QVERIFY(!std::filesystem::exists(root_ / "scripts" / "menu.asi"));
    QVERIFY(!manager().removePlugin("../outside.asi"));
    QVERIFY(!manager().removePlugin("GTA5.exe"));
}

void TestAsiManager::addPluginRefusesNonAsi()
{
    const std::vector<std::filesystem::path> bad { source("tool.dll", "dll") };
    QVERIFY(!manager().addPlugins(bad));
    const std::vector<std::filesystem::path> good { source("one.asi", "1"), source("Two.ASI", "2") };
    QVERIFY(manager().addPlugins(good));
    QCOMPARE(toText(readFile(root_ / "one.asi")), std::string("1"));
    QCOMPARE(toText(readFile(root_ / "Two.ASI")), std::string("2"));
    QCOMPARE(listTransactions(store_).front().description, std::string("Add 2 ASI plugins"));
}

void TestAsiManager::everyActionIsOneHistoryEntry()
{
    QVERIFY(manager().installLoader(bytesOf("loader")));
    QVERIFY(manager().installModsLoader(bytesOf("rage")));
    const std::vector<std::filesystem::path> plugin { source("x.asi", "x") };
    QVERIFY(manager().addPlugins(plugin));
    QVERIFY(manager().setPluginEnabled("x.asi", false));
    QVERIFY(manager().setPluginEnabled("x.asi.disabled", true));
    QVERIFY(manager().removePlugin("x.asi"));
    QVERIFY(manager().uninstallModsLoader());
    QVERIFY(manager().uninstallLoader());
    const auto history = listTransactions(store_);
    QStringList descriptions;
    for (auto record = history.rbegin(); record != history.rend(); ++record)
        descriptions.append(QString::fromStdString(record->description));
    QCOMPARE(descriptions, QStringList({ "Install ASI Loader", "Install mods folder loader", "Add 1 ASI plugin", "Disable ASI plugin x.asi", "Enable ASI plugin x.asi", "Remove ASI plugin x.asi", "Uninstall mods folder loader", "Uninstall ASI Loader" }));
    for (auto record = history.rbegin(); record != history.rend(); ++record)
        QVERIFY(record->committed);
    QVERIFY(rollbackTransaction(store_, history.back().id));
    QVERIFY(!std::filesystem::exists(root_ / "dinput8.dll"));
    QVERIFY(!std::filesystem::exists(root_ / "mods"));
}

QTEST_APPLESS_MAIN(TestAsiManager)
#include "tst_asi_manager.moc"
