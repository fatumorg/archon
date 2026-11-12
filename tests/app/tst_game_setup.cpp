#include "app/game_setup.h"
#include "support/browse_fixture.h"
#include "support/real_install.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QFile>
#include <QSettings>
#include <QTest>

using namespace archon;
using namespace archon::test;

class TestGameSetup : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void checkMagicRejectsWrongBytes();
    void checkMagicAcceptsPinnedFile();
    void savedInstallIsReused();
    void storeLockAllowsOneSession();

private:
    QTemporaryDir home_;
};

void TestGameSetup::initTestCase()
{
    qputenv("HOME", home_.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home_.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home_.path() + QStringLiteral("/data")).toUtf8());
}

void TestGameSetup::checkMagicRejectsWrongBytes()
{
    QVERIFY(!checkMagic({}));
    const auto wrong = checkMagic(asBytes("this is not the key table file"));
    QVERIFY(!wrong);
    QVERIFY(wrong.error().message.find("SHA-256") != std::string::npos);
}

void TestGameSetup::checkMagicAcceptsPinnedFile()
{
    if (!realMagicDat())
        QSKIP("ARCHON_MAGIC_DAT is not set");
    QFile file(QString::fromStdString(realMagicDat()->string()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    QVERIFY(checkMagic(std::as_bytes(std::span(content.constData(), static_cast<std::size_t>(content.size())))));
}

void TestGameSetup::savedInstallIsReused()
{
    const std::filesystem::path root = pathOf(home_) / "game";
    writeBrowseFixture(root);
    QSettings(QStringLiteral("archon"), QStringLiteral("archon")).setValue(QStringLiteral("game/path"), QString::fromStdString(root.string()));
    const auto install = chooseInstall(nullptr);
    QVERIFY(install.has_value());
    QCOMPARE(install->root, root);
    QCOMPARE(install->edition, GameEdition::Legacy);
}

void TestGameSetup::storeLockAllowsOneSession()
{
    QTemporaryDir directory;
    const std::filesystem::path store = std::filesystem::path(directory.path().toStdString()) / "safety" / "abc";
    auto first = lockStore(store);
    QVERIFY(first != nullptr);
    QVERIFY(lockStore(store) == nullptr);
    first.reset();
    QVERIFY(lockStore(store) != nullptr);
}

QTEST_MAIN(TestGameSetup)
#include "tst_game_setup.moc"
