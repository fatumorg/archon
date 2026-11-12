#include "app/package_installer_dialog.h"
#include "app/style.h"
#include "core/rpf/rpf_reader.h"
#include "core/safety/transaction.h"
#include "support/browse_fixture.h"
#include "support/modal_driver.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"
#include "support/zip_builder.h"

#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTest>
#include <QTextBrowser>

using namespace archon;
using namespace archon::test;

class TestPackageInstaller : public QObject {
    Q_OBJECT
private slots:
    void init();
    void showsPackageHeaderAndPages();
    void installsToModsFolder();
    void installsToGameFolder();
    void failureIsShownAndRolledBack();
    void legacyPackageOffersContentChoice();
    void brokenPackageIsInvalid();
    void styledNameIsLarger();

private:
    std::filesystem::path writePackage(const std::string &assembly, std::vector<std::pair<std::string, std::string>> files)
    {
        files.emplace_back("assembly.xml", assembly);
        const std::filesystem::path file = pathOf(*directory_) / ("package" + std::to_string(++packages_) + ".oiv");
        writeFile(file, makeZip(files));
        return file;
    }
    std::unique_ptr<PackageInstallerDialog> dialog(const std::filesystem::path &package)
    {
        auto made = std::make_unique<PackageInstallerDialog>(package, root_, nullptr, store_);
        made->show();
        return made;
    }
    QString label(PackageInstallerDialog &shown, const char *name)
    {
        auto *found = shown.findChild<QLabel *>(QString::fromLatin1(name));
        return found != nullptr ? found->text() : QStringLiteral("<no %1>").arg(QString::fromLatin1(name));
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::filesystem::path store_;
    int packages_ = 0;
};

namespace {

std::string assembly22(const std::string &content, bool blackText = false)
{
    return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>
<package version="2.2" id="{1}" target="Five"><metadata><name>Better Adder</name><version><major>2</major><minor>3</minor><tag>Beta</tag></version>
<author><displayName>Modder</displayName></author><description><![CDATA[Makes the Adder faster.]]></description>
<largeDescription displayName="Version history"><![CDATA[2.3: faster.]]></largeDescription><licence><![CDATA[Free to use.]]></licence></metadata>
<colors><headerBackground useBlackTextColor=")") + (blackText ? "True" : "False") + R"(">$FF23366A</headerBackground><iconBackground>$FF3b5998</iconBackground></colors>
<content>)" + content + "</content></package>";
}

std::string entry(const std::filesystem::path &file, std::string_view path)
{
    auto reader = RpfReader::open(openFileSource(file), file.filename().string(), nullptr);
    if (!reader)
        return "<" + reader.error().message + ">";
    const auto index = reader->archive().find(path);
    if (!index)
        return "<missing>";
    const auto data = reader->fileData(*index);
    return data ? toText(*data) : "<unreadable>";
}

}

void TestPackageInstaller::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    store_ = pathOf(*directory_) / "store";
    writeBrowseFixture(root_);
}

void TestPackageInstaller::showsPackageHeaderAndPages()
{
    auto shown = dialog(writePackage(assembly22(""), { { "icon.png", "notapng" } }));
    QVERIFY(shown->isValid());
    QCOMPARE(shown->windowTitle(), QStringLiteral("Package Installer"));
    QCOMPARE(label(*shown, "packageName"), QStringLiteral("Better Adder"));
    QCOMPARE(label(*shown, "packageVersion"), QStringLiteral("Version 2.3 Beta"));
    QCOMPARE(label(*shown, "packageAuthor"), QStringLiteral("by Modder"));
    auto *header = shown->findChild<QWidget *>(QStringLiteral("packageHeader"));
    QVERIFY(header != nullptr);
    QCOMPARE(header->palette().color(QPalette::Window), QColor::fromRgba(0xFF23366A));
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("packageName"))->palette().color(QPalette::WindowText), QColor(Qt::white));
    auto *pages = shown->findChild<QTabWidget *>(QStringLiteral("packagePages"));
    QVERIFY(pages != nullptr);
    QStringList titles;
    for (int index = 0; index < pages->count(); ++index)
        titles.append(pages->tabText(index));
    QCOMPARE(titles, QStringList({ "Description", "Version history", "Licence" }));
    QVERIFY(qobject_cast<QTextBrowser *>(pages->widget(0))->toPlainText().contains(QStringLiteral("Makes the Adder faster.")));

    auto black = dialog(writePackage(assembly22("", true), {}));
    QCOMPARE(black->findChild<QLabel *>(QStringLiteral("packageName"))->palette().color(QPalette::WindowText), QColor(Qt::black));
}

void TestPackageInstaller::installsToModsFolder()
{
    const auto original = readFile(root_ / "common.rpf");
    auto shown = dialog(writePackage(assembly22(R"(<archive path="common.rpf" createIfNotExist="False" type="RPF7"><add source="a.txt">mod\a.txt</add></archive>)"), { { "content/a.txt", "modded" } }));
    shown->findChild<QPushButton *>(QStringLiteral("installButton"))->click();
    QCOMPARE(shown->findChild<QStackedWidget *>(QStringLiteral("installerSteps"))->currentIndex(), 1);
    auto *mods = shown->findChild<QPushButton *>(QStringLiteral("modsTargetButton"));
    QVERIFY(mods != nullptr && mods->isDefault());
    mods->click();
    QCOMPARE(label(*shown, "resultText"), QStringLiteral("Package installed."));
    QCOMPARE(entry(root_ / "mods" / "common.rpf", "mod/a.txt"), std::string("modded"));
    QCOMPARE(readFile(root_ / "common.rpf"), original);
    QCOMPARE(listTransactions(store_).front().description, std::string("Install package Better Adder 2.3"));
}

void TestPackageInstaller::installsToGameFolder()
{
    auto shown = dialog(writePackage(assembly22(R"(<add source="mod.asi">mod.asi</add>)"), { { "content/mod.asi", "asi" } }));
    shown->install(InstallTarget::Game);
    QCOMPARE(label(*shown, "resultText"), QStringLiteral("Package installed."));
    QCOMPARE(toText(readFile(root_ / "mod.asi")), std::string("asi"));
}

void TestPackageInstaller::failureIsShownAndRolledBack()
{
    auto shown = dialog(writePackage(assembly22(R"(<add source="mod.asi">mod.asi</add><add source="gone.asi">gone.asi</add>)"), { { "content/mod.asi", "asi" } }));
    shown->install(InstallTarget::Game);
    QVERIFY(label(*shown, "resultText").startsWith(QStringLiteral("The package was not installed. Nothing was changed.")));
    QVERIFY(label(*shown, "resultText").contains(QStringLiteral("gone.asi")));
    QVERIFY(!std::filesystem::exists(root_ / "mod.asi"));
    QVERIFY(listTransactions(store_).empty());
}

void TestPackageInstaller::legacyPackageOffersContentChoice()
{
    const std::string assembly = R"(<package version="1.1"><metadata><name>Old</name><author>W</author><target><game>Five</game></target><description>Short</description></metadata>
<content gameID="Five" name="Red" description="Red paint"><add source="content\red.txt">paint.txt</add></content>
<content gameID="Five" name="Blue" description="Blue paint"><add source="content\blue.txt">paint.txt</add></content></package>)";
    auto shown = dialog(writePackage(assembly, { { "content/red.txt", "red" }, { "content/blue.txt", "blue" } }));
    auto *red = shown->findChild<QRadioButton *>(QStringLiteral("content0"));
    auto *blue = shown->findChild<QRadioButton *>(QStringLiteral("content1"));
    QVERIFY(red != nullptr && blue != nullptr);
    QVERIFY(red->isChecked());
    QVERIFY(red->text().contains(QStringLiteral("Red")));
    blue->setChecked(true);
    shown->install(InstallTarget::Game);
    QCOMPARE(toText(readFile(root_ / "paint.txt")), std::string("blue"));
    QCOMPARE(listTransactions(store_).front().description, std::string("Install package Old"));
}

void TestPackageInstaller::brokenPackageIsInvalid()
{
    const std::filesystem::path garbage = pathOf(*directory_) / "broken.oiv";
    writeFile(garbage, "not a zip");
    auto shown = std::make_unique<PackageInstallerDialog>(garbage, root_, nullptr, store_);
    QVERIFY(!shown->isValid());
    QVERIFY(!shown->errorText().isEmpty());
    auto other = std::make_unique<PackageInstallerDialog>(writePackage(R"(<package version="2.2" id="{1}" target="IV"><metadata><name>x</name></metadata><content/></package>)", {}), root_, nullptr, store_);
    QVERIFY(!other->isValid());
    QCOMPARE(other->errorText(), QStringLiteral("This package is not for GTA V (it targets IV)."));
}

void TestPackageInstaller::styledNameIsLarger()
{
    applyStyle(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    auto shown = dialog(writePackage(assembly22(""), {}));
    QVERIFY(QTest::qWaitForWindowExposed(shown.get()));
    const qreal name = shown->findChild<QLabel *>(QStringLiteral("packageName"))->fontInfo().pointSizeF();
    const qreal author = shown->findChild<QLabel *>(QStringLiteral("packageAuthor"))->fontInfo().pointSizeF();
    QVERIFY2(name > author * 1.5, qPrintable(QStringLiteral("%1 vs %2").arg(name).arg(author)));
}

QTEST_MAIN(TestPackageInstaller)
#include "tst_package_installer.moc"
