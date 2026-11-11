#include "core/io/path_resolver.h"
#include "support/temp_files.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestPathResolver : public QObject {
    Q_OBJECT
private slots:
    void resolvesMixedCase();
    void prefersExactSpelling();
};

void TestPathResolver::resolvesMixedCase()
{
    QTemporaryDir directory;
    const auto root = pathOf(directory);
    writeFile(root / "Update" / "X64" / "dlcpacks" / "a.rpf");
    QCOMPARE(resolveCaseInsensitive(root, "update\\x64\\DLCPACKS\\A.RPF"), std::optional(root / "Update" / "X64" / "dlcpacks" / "a.rpf"));
    QCOMPARE(resolveCaseInsensitive(root, "UPDATE/x64"), std::optional(root / "Update" / "X64"));
    QCOMPARE(resolveCaseInsensitive(root, ""), std::optional(root));
    QVERIFY(!resolveCaseInsensitive(root, "../x"));
    QVERIFY(!resolveCaseInsensitive(root, "/etc/passwd"));
    QVERIFY(!resolveCaseInsensitive(root, "\\etc\\passwd"));
    QVERIFY(!resolveCaseInsensitive(root, "update/../../x"));
    QVERIFY(!resolveCaseInsensitive(root, "update\\..\\x64"));
    QVERIFY(!resolveCaseInsensitive(root, "update/x64/missing.rpf"));
    QVERIFY(!resolveCaseInsensitive(root, "update/x64/dlcpacks/a.rpf/deeper"));
}

void TestPathResolver::prefersExactSpelling()
{
    QTemporaryDir directory;
    const auto root = pathOf(directory);
    writeFile(root / "File.txt");
    writeFile(root / "file.txt");
    QCOMPARE(resolveCaseInsensitive(root, "file.txt"), std::optional(root / "file.txt"));
    QCOMPARE(resolveCaseInsensitive(root, "File.txt"), std::optional(root / "File.txt"));
}

QTEST_APPLESS_MAIN(TestPathResolver)
#include "tst_path_resolver.moc"
