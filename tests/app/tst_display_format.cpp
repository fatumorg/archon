#include "app/display_format.h"
#include "app/style.h"

#include <QTest>

using namespace archon;

namespace {

BrowserNode node(std::string name, NodeKind kind, std::uint64_t size = 0)
{
    return BrowserNode { std::move(name), kind, size, true, false, false, false };
}

}

class TestDisplayFormat : public QObject {
    Q_OBJECT
private slots:
    void formatsSizes();
    void sizeTextCountsFolderItems();
    void namesTypes();
    void listsAttributes();
    void everyIconRenders();
};

void TestDisplayFormat::formatsSizes()
{
    QCOMPARE(formatSize(0), QStringLiteral("0 bytes"));
    QCOMPARE(formatSize(393), QStringLiteral("393 bytes"));
    QCOMPARE(formatSize(1023), QStringLiteral("1023 bytes"));
    QCOMPARE(formatSize(1048), QStringLiteral("1.023 Kb"));
    QCOMPARE(formatSize(94474), QStringLiteral("92.26 Kb"));
    QCOMPARE(formatSize(20685), QStringLiteral("20.2 Kb"));
    QCOMPARE(formatSize(5 * 1024 * 1024), QStringLiteral("5 Mb"));
    QCOMPARE(formatSize(3ull * 1024 * 1024 * 1024 / 2), QStringLiteral("1.5 Gb"));
    QCOMPARE(formatSize(5000ull * 1024 * 1024 * 1024), QStringLiteral("5000 Gb"));
}

void TestDisplayFormat::sizeTextCountsFolderItems()
{
    QCOMPARE(sizeText(node("data", NodeKind::Folder, 12)), QStringLiteral("12 items"));
    QCOMPARE(sizeText(node("maps", NodeKind::Folder, 1)), QStringLiteral("1 items"));
    QCOMPARE(sizeText(node("a.xml", NodeKind::File, 393)), QStringLiteral("393 bytes"));
}

void TestDisplayFormat::namesTypes()
{
    QCOMPARE(typeName(node("data", NodeKind::Folder)), QStringLiteral("Folder"));
    QCOMPARE(typeName(node("x64a.RPF", NodeKind::Archive)), QStringLiteral("RPF Archive"));
    QCOMPARE(typeName(node("clouds.xml", NodeKind::File)), QStringLiteral("XML text"));
    QCOMPARE(typeName(node("common.META", NodeKind::File)), QStringLiteral("Meta (XML)"));
    QCOMPARE(typeName(node("a.ytd", NodeKind::File)), QStringLiteral("Texture dictionary"));
    QCOMPARE(typeName(node("a.ydr", NodeKind::File)), QStringLiteral("Model"));
    QCOMPARE(typeName(node("a.yft", NodeKind::File)), QStringLiteral("Model"));
    QCOMPARE(typeName(node("handling.dat", NodeKind::File)), QStringLiteral("DAT file"));
    QCOMPARE(typeName(node("b.bin", NodeKind::File)), QStringLiteral("BIN file"));
    QCOMPARE(typeName(node("README", NodeKind::File)), QStringLiteral("File"));
    QCOMPARE(typeName(node(".hidden", NodeKind::File)), QStringLiteral("File"));
}

void TestDisplayFormat::listsAttributes()
{
    QCOMPARE(attributesText(node("data", NodeKind::Folder)), QStringLiteral("Folder"));
    QCOMPARE(attributesText(node("a.txt", NodeKind::File)), QString());
    BrowserNode packed = node("a.xml", NodeKind::File);
    packed.compressed = true;
    packed.encrypted = true;
    QCOMPARE(attributesText(packed), QStringLiteral("Compressed;Encrypted;"));
    BrowserNode resource = node("a.ytd", NodeKind::File);
    resource.resource = true;
    QCOMPARE(attributesText(resource), QStringLiteral("Resource;"));
}

void TestDisplayFormat::everyIconRenders()
{
    for (const char *name : { "archon", "back", "forward", "up", "folder", "archive", "file", "extract", "search", "refresh" }) {
        const QImage image = loadIcon(QLatin1StringView(name)).pixmap(32, 32).toImage();
        QVERIFY2(!image.isNull(), name);
        bool painted = false;
        for (int y = 0; y < image.height() && !painted; ++y) {
            for (int x = 0; x < image.width() && !painted; ++x)
                painted = qAlpha(image.pixel(x, y)) != 0;
        }
        QVERIFY2(painted, name);
    }
}

QTEST_MAIN(TestDisplayFormat)
#include "tst_display_format.moc"
