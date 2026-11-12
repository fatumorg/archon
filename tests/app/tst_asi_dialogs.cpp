#include "app/asi_manager_dialog.h"
#include "app/prefix_setup_dialog.h"
#include "app/style.h"
#include "core/asi/asi_manager.h"
#include "core/safety/transaction.h"
#include "support/modal_driver.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

using namespace archon;
using namespace archon::test;

namespace {

QByteArray fileBytes(const std::filesystem::path &file)
{
    QFile opened(file);
    return opened.open(QIODevice::ReadOnly) ? opened.readAll() : QByteArray();
}

QTreeWidgetItem *row(QTreeWidget *list, const QString &name)
{
    const auto found = list->findItems(name, Qt::MatchExactly, 0);
    return found.isEmpty() ? nullptr : found.first();
}

const char *const fakeProtontricks = R"(#!/bin/sh
echo "fake protontricks $*"
if [ -n "$FAKE_SLEEP" ]; then sleep "$FAKE_SLEEP"; fi
if [ -n "$FAKE_START_GAME" ]; then mkdir -p "$FAKE_START_GAME/555"; printf 'Z:\\GTA5.exe\0' > "$FAKE_START_GAME/555/cmdline"; fi
echo "$*" >> "$FAKE_LOG"
p="$FAKE_PREFIX"
if [ "$1" = "-c" ]; then
  printf '[Software\\\\Wine\\\\DllOverrides] 1\n"dinput8"="native,builtin"\n\n' >> "$p/user.reg"
  exit 0
fi
shift
for verb in "$@"; do
  if [ "$verb" = "$FAKE_FAIL_VERB" ]; then exit 3; fi
  case "$verb" in
    dotnet48) mkdir -p "$p/drive_c/windows/Microsoft.NET/Framework64/v4.0.30319"; echo MZnative > "$p/drive_c/windows/Microsoft.NET/Framework64/v4.0.30319/clr.dll" ;;
    vcrun2022) echo MZnative > "$p/drive_c/windows/system32/msvcp140.dll"; echo MZnative > "$p/drive_c/windows/system32/vcruntime140.dll" ;;
    d3dcompiler_47) rm -f "$p/drive_c/windows/system32/d3dcompiler_47.dll"; echo MZnative > "$p/drive_c/windows/system32/d3dcompiler_47.dll" ;;
    win10) printf '[Software\\\\Wine] 1\n"Version"="win10"\n\n' >> "$p/user.reg" ;;
  esac
done
exit 0
)";

}

class TestAsiDialogs : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void wrongBytesAreRefusedAndNothingWritten();
    void installsPinnedDownloads();
    void uninstallsLoader();
    void pluginButtonsWork();
    void prefixSetupRunsMissingVerbsAndVerifies();
    void prefixSetupReportsFailure();
    void prefixSetupRefusesWhileGameRuns();
    void prefixSetupReportsMissingProtontricks();
    void layoutsHoldTheirContent();
    void prefixDialogStaysOpenWhileRunning();
    void prefixSetupStopsWhenGameStarts();
    void otherLoaderOffersInstall();

private:
    std::unique_ptr<AsiManagerDialog> dialog()
    {
        auto made = std::make_unique<AsiManagerDialog>(root_, GameEdition::Legacy, store_);
        made->show();
        return made;
    }
    std::unique_ptr<PrefixSetupDialog> prefixDialog()
    {
        auto made = std::make_unique<PrefixSetupDialog>(prefix_, GameEdition::Legacy, store_ / "prefix-271590.log", proc_);
        made->show();
        return made;
    }
    QString output(PrefixSetupDialog &dialog) const
    {
        return dialog.findChild<QPlainTextEdit *>(QStringLiteral("prefixOutput"))->toPlainText();
    }
    QString componentState(PrefixSetupDialog &dialog, int index) const
    {
        return dialog.findChild<QTreeWidget *>(QStringLiteral("componentList"))->topLevelItem(index)->text(1);
    }
    bool runSetup(PrefixSetupDialog &dialog)
    {
        QSignalSpy finished(&dialog, &PrefixSetupDialog::setupFinished);
        dialog.start();
        if (finished.isEmpty() && !finished.wait(20000))
            return false;
        return finished.first().first().toBool();
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::filesystem::path store_;
    std::filesystem::path prefix_;
    std::filesystem::path proc_;
    std::filesystem::path bin_;
    QByteArray savedPath_;
};

void TestAsiDialogs::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    store_ = pathOf(*directory_) / "store";
    prefix_ = pathOf(*directory_) / "pfx";
    proc_ = pathOf(*directory_) / "proc";
    bin_ = pathOf(*directory_) / "bin";
    writeFile(root_ / "GTA5.exe", "exe");
    writeFile(prefix_ / "user.reg", "WINE REGISTRY Version 2\n\n");
    writeFile(prefix_ / "system.reg", "WINE REGISTRY Version 2\n\n");
    std::string builtin(256, '\0');
    builtin.replace(64, 16, "Wine builtin DLL");
    writeFile(prefix_ / "drive_c" / "windows" / "system32" / "d3dcompiler_47.dll", builtin);
    writeFile(proc_ / "1" / "cmdline", std::string("/usr/bin/steam\0", 15));
    writeFile(bin_ / "protontricks", fakeProtontricks);
    std::filesystem::permissions(bin_ / "protontricks", std::filesystem::perms::owner_all);
    savedPath_ = qgetenv("PATH");
    qputenv("PATH", QByteArray::fromStdString(bin_.string()) + ':' + savedPath_);
    qputenv("FAKE_PREFIX", QByteArray::fromStdString(prefix_.string()));
    qputenv("FAKE_LOG", QByteArray::fromStdString((pathOf(*directory_) / "calls.log").string()));
    qunsetenv("FAKE_FAIL_VERB");
    qunsetenv("FAKE_SLEEP");
    qunsetenv("FAKE_START_GAME");
}

void TestAsiDialogs::cleanup()
{
    qputenv("PATH", savedPath_);
}

void TestAsiDialogs::wrongBytesAreRefusedAndNothingWritten()
{
    auto shown = dialog();
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("loaderStatus"))->text(), QStringLiteral("Not installed"));
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("modsLoaderStatus"))->text(), QStringLiteral("Not installed"));
    ModalDriver driver({ QStringLiteral("OK") });
    shown->installFrom(asiLoaderDownload(), QByteArray("tampered"));
    QVERIFY(driver.done());
    QCOMPARE(driver.seen, QStringList { QStringLiteral("ASI Loader does not match the pinned SHA-256") });
    QVERIFY(!std::filesystem::exists(root_ / "dinput8.dll"));
    QVERIFY(listTransactions(store_).empty());
}

void TestAsiDialogs::installsPinnedDownloads()
{
    const QByteArray folder = qgetenv("ARCHON_ASI_DOWNLOADS");
    if (folder.isEmpty())
        QSKIP("ARCHON_ASI_DOWNLOADS is not set");
    const std::filesystem::path downloads(folder.toStdString());
    auto shown = dialog();
    ModalDriver driver({});
    shown->installFrom(asiLoaderDownload(), fileBytes(downloads / "Ultimate-ASI-Loader-NoPDB_x64.zip"));
    shown->installFrom(modsLoaderDownload(), fileBytes(downloads / "RageOpenV.zip"));
    QVERIFY2(driver.unexpected.isEmpty(), qPrintable(driver.unexpected.join(QLatin1Char('\n'))));
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("loaderStatus"))->text(), QStringLiteral("Installed (Ultimate ASI Loader v10.0.2)"));
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("modsLoaderStatus"))->text(), QStringLiteral("Installed (RageOpenV.asi)"));
    QCOMPARE(shown->findChild<QPushButton *>(QStringLiteral("loaderButton"))->text(), QStringLiteral("Uninstall"));
    QCOMPARE(std::filesystem::file_size(root_ / "dinput8.dll"), std::uintmax_t(2435816));
    QVERIFY(std::filesystem::is_directory(root_ / "mods"));
}

void TestAsiDialogs::uninstallsLoader()
{
    writeFile(root_ / "OpenIV.asi", "openiv");
    auto shown = dialog();
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("modsLoaderStatus"))->text(), QStringLiteral("Installed (OpenIV.asi)"));
    auto *button = shown->findChild<QPushButton *>(QStringLiteral("modsLoaderButton"));
    QCOMPARE(button->text(), QStringLiteral("Uninstall"));
    ModalDriver driver({ QStringLiteral("Yes") });
    button->click();
    QVERIFY(driver.done());
    QVERIFY(!std::filesystem::exists(root_ / "OpenIV.asi"));
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("modsLoaderStatus"))->text(), QStringLiteral("Not installed"));
}

void TestAsiDialogs::pluginButtonsWork()
{
    writeFile(root_ / "scripts" / "menu.asi", "menu");
    auto shown = dialog();
    auto *plugins = shown->findChild<QTreeWidget *>(QStringLiteral("pluginList"));
    auto *toggle = shown->findChild<QPushButton *>(QStringLiteral("toggleButton"));
    auto *remove = shown->findChild<QPushButton *>(QStringLiteral("removeButton"));
    QVERIFY(plugins != nullptr && toggle != nullptr && remove != nullptr);
    QTreeWidgetItem *menu = row(plugins, QStringLiteral("menu.asi"));
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->text(1), QStringLiteral("scripts"));
    QCOMPARE(menu->text(2), QStringLiteral("Enabled"));
    QVERIFY(!toggle->isEnabled());
    plugins->setCurrentItem(menu);
    QCOMPARE(toggle->text(), QStringLiteral("Disable"));
    toggle->click();
    QVERIFY(std::filesystem::exists(root_ / "scripts" / "menu.asi.disabled"));
    menu = row(plugins, QStringLiteral("menu.asi"));
    QCOMPARE(menu->text(2), QStringLiteral("Disabled"));
    plugins->setCurrentItem(menu);
    QCOMPARE(toggle->text(), QStringLiteral("Enable"));
    toggle->click();
    QVERIFY(std::filesystem::exists(root_ / "scripts" / "menu.asi"));

    const std::filesystem::path added = pathOf(*directory_) / "sources" / "extra.asi";
    writeFile(added, "extra");
    shown->addPlugins({ QString::fromStdString(added.string()) });
    QVERIFY(row(plugins, QStringLiteral("extra.asi")) != nullptr);
    QVERIFY(std::filesystem::exists(root_ / "extra.asi"));

    plugins->setCurrentItem(row(plugins, QStringLiteral("menu.asi")));
    ModalDriver driver({ QStringLiteral("Yes") });
    remove->click();
    QVERIFY(driver.done());
    QVERIFY(row(plugins, QStringLiteral("menu.asi")) == nullptr);
    QVERIFY(!std::filesystem::exists(root_ / "scripts" / "menu.asi"));
    QCOMPARE(listTransactions(store_).size(), std::size_t(4));
}

void TestAsiDialogs::prefixSetupRunsMissingVerbsAndVerifies()
{
    auto shown = prefixDialog();
    QVERIFY(shown->findChild<QLabel *>(QStringLiteral("prefixWarning"))->text().contains(QStringLiteral("Changes to the Proton prefix cannot be rolled back.")));
    QCOMPARE(componentState(*shown, 0), QStringLiteral("Missing"));
    QVERIFY(runSetup(*shown));
    for (int index = 0; index < 5; ++index)
        QCOMPARE(componentState(*shown, index), QStringLiteral("Present"));
    QVERIFY(output(*shown).contains(QStringLiteral("$ protontricks 271590 -q dotnet48\nfake protontricks 271590 -q dotnet48")));
    const QByteArray calls = fileBytes(pathOf(*directory_) / "calls.log");
    QCOMPARE(calls.count('\n'), 5);
    const QByteArray record = fileBytes(store_ / "prefix-271590.log");
    QVERIFY(record.contains("protontricks 271590 -q win10 -> exit code 0"));
    QVERIFY(record.contains("exit code 0"));
    QVERIFY(record.contains(".NET Framework 4.8 (ScriptHookVDotNet, RAGE Plugin Hook, LSPDFR): present"));

    QVERIFY(runSetup(*shown));
    QCOMPARE(fileBytes(pathOf(*directory_) / "calls.log").count('\n'), 5);
    QVERIFY(output(*shown).contains(QStringLiteral("Everything is already set up.")));
}

void TestAsiDialogs::prefixSetupReportsFailure()
{
    qputenv("FAKE_FAIL_VERB", "dotnet48");
    auto shown = prefixDialog();
    QVERIFY(!runSetup(*shown));
    QVERIFY(output(*shown).contains(QStringLiteral("protontricks exited with code 3")));
    QVERIFY(output(*shown).contains(QStringLiteral("Not installed: .NET Framework 4.8 (ScriptHookVDotNet, RAGE Plugin Hook, LSPDFR)")));
    QVERIFY(fileBytes(store_ / "prefix-271590.log").contains("protontricks 271590 -q dotnet48 -> exit code 3"));
    QCOMPARE(fileBytes(pathOf(*directory_) / "calls.log").count('\n'), 5);
    QCOMPARE(componentState(*shown, 0), QStringLiteral("Missing"));
    for (int index = 1; index < 5; ++index)
        QCOMPARE(componentState(*shown, index), QStringLiteral("Present"));
}

void TestAsiDialogs::prefixSetupRefusesWhileGameRuns()
{
    writeFile(proc_ / "999" / "cmdline", std::string("Z:\\games\\GTA5.exe\0", 18));
    auto shown = prefixDialog();
    QVERIFY(!runSetup(*shown));
    QVERIFY(output(*shown).contains(QStringLiteral("Close the game before setting up its prefix.")));
    QVERIFY(!std::filesystem::exists(pathOf(*directory_) / "calls.log"));
}

void TestAsiDialogs::prefixSetupReportsMissingProtontricks()
{
    qputenv("PATH", QByteArray::fromStdString((pathOf(*directory_) / "empty").string()));
    auto shown = prefixDialog();
    QVERIFY(!runSetup(*shown));
    QVERIFY(output(*shown).contains(QStringLiteral("protontricks was not found. Install it and try again.")));
    QVERIFY(!std::filesystem::exists(pathOf(*directory_) / "calls.log"));
}

void TestAsiDialogs::layoutsHoldTheirContent()
{
    applyStyle(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    auto shown = dialog();
    QVERIFY(QTest::qWaitForWindowExposed(shown.get()));
    const int width = shown->width();
    const QByteArray folder = qgetenv("ARCHON_ASI_DOWNLOADS");
    if (folder.isEmpty()) {
        writeFile(root_ / "dinput8.dll", "other loader");
    } else {
        ModalDriver driver({});
        shown->installFrom(asiLoaderDownload(), fileBytes(std::filesystem::path(folder.toStdString()) / "Ultimate-ASI-Loader-NoPDB_x64.zip"));
    }
    writeFile(root_ / "RageOpenV.asi", "rage");
    const std::filesystem::path added = pathOf(*directory_) / "sources" / "extra.asi";
    writeFile(added, "extra");
    shown->addPlugins({ QString::fromStdString(added.string()) });
    QCoreApplication::processEvents();
    QCOMPARE(shown->width(), width);

    auto prefix = prefixDialog();
    QVERIFY(QTest::qWaitForWindowExposed(prefix.get()));
    auto *components = prefix->findChild<QTreeWidget *>(QStringLiteral("componentList"));
    QCOMPARE(components->verticalScrollBar()->maximum(), 0);
}

void TestAsiDialogs::prefixDialogStaysOpenWhileRunning()
{
    qputenv("FAKE_SLEEP", "1");
    auto shown = prefixDialog();
    QSignalSpy finished(shown.get(), &PrefixSetupDialog::setupFinished);
    shown->start();
    QTest::qWait(200);
    shown->reject();
    shown->close();
    QTest::keyClick(shown.get(), Qt::Key_Escape);
    QVERIFY(shown->isVisible());
    QVERIFY(finished.wait(30000));
    QVERIFY(fileBytes(store_ / "prefix-271590.log").contains("exit code 0"));
    shown->close();
    QVERIFY(!shown->isVisible());
}

void TestAsiDialogs::prefixSetupStopsWhenGameStarts()
{
    qputenv("FAKE_START_GAME", QByteArray::fromStdString(proc_.string()));
    auto shown = prefixDialog();
    QVERIFY(!runSetup(*shown));
    QCOMPARE(fileBytes(pathOf(*directory_) / "calls.log").count('\n'), 1);
    QVERIFY(output(*shown).contains(QStringLiteral("Close the game before setting up its prefix.")));
}

void TestAsiDialogs::otherLoaderOffersInstall()
{
    writeFile(root_ / "dinput8.dll", "scripthook loader");
    auto shown = dialog();
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("loaderStatus"))->text(), QStringLiteral("Installed (other version)"));
    QCOMPARE(shown->findChild<QPushButton *>(QStringLiteral("loaderButton"))->text(), QStringLiteral("Install"));
}

QTEST_MAIN(TestAsiDialogs)
#include "tst_asi_dialogs.moc"
