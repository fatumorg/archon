#include "core/ytd/texture_edit.h"

#include <QTest>

using namespace archon;

class TestTextureEdit : public QObject {
    Q_OBJECT
private slots:
    void nameRule();
    void addsAndRefusesDuplicates();
    void replaceKeepsNameAndUsage();
    void removesAndRenames();
};

namespace {

Texture texture(std::string name, std::uint32_t usage = 0x20000014)
{
    return { std::move(name), 4, 4, 1, 0x31545844, usage, std::vector<std::byte>(8) };
}

}

void TestTextureEdit::nameRule()
{
    QVERIFY(isValidTextureName("prop_chrome_03"));
    QVERIFY(isValidTextureName("a"));
    QVERIFY(!isValidTextureName(""));
    QVERIFY(!isValidTextureName("a/b"));
    QVERIFY(!isValidTextureName("a\\b"));
    QVERIFY(!isValidTextureName("with space"));
    QVERIFY(!isValidTextureName(std::string(64, 'a')));
    QVERIFY(isValidTextureName(std::string(63, 'a')));
    QVERIFY(!isValidTextureName("caf\xc3\xa9"));
}

void TestTextureEdit::addsAndRefusesDuplicates()
{
    std::vector<Texture> textures { texture("first") };
    QVERIFY(addTexture(textures, texture("second")));
    QCOMPARE(textures.size(), std::size_t(2));
    const auto clash = addTexture(textures, texture("FIRST"));
    QVERIFY(!clash);
    QCOMPARE(clash.error().message, std::string("a texture named FIRST already exists"));
    QVERIFY(!addTexture(textures, texture("bad name")));
}

void TestTextureEdit::replaceKeepsNameAndUsage()
{
    std::vector<Texture> textures { texture("diffuse", 0x2002b054) };
    Texture replacement { "imported_file", 8, 8, 2, 21, 0x20000014, std::vector<std::byte>(320) };
    QVERIFY(replaceTexture(textures, "DIFFUSE", replacement));
    QCOMPARE(textures[0].name, std::string("diffuse"));
    QCOMPARE(textures[0].usage, 0x2002b054u);
    QCOMPARE(textures[0].width, 8u);
    QCOMPARE(textures[0].format, 21u);
    QVERIFY(!replaceTexture(textures, "missing", replacement));
}

void TestTextureEdit::removesAndRenames()
{
    std::vector<Texture> textures { texture("a"), texture("b"), texture("c") };
    QVERIFY(removeTexture(textures, "B"));
    QCOMPARE(textures.size(), std::size_t(2));
    QVERIFY(!removeTexture(textures, "b"));
    QVERIFY(renameTexture(textures, "a", "renamed"));
    QCOMPARE(textures[0].name, std::string("renamed"));
    QVERIFY(renameTexture(textures, "renamed", "RENAMED"));
    QCOMPARE(textures[0].name, std::string("RENAMED"));
    QVERIFY(!renameTexture(textures, "RENAMED", "C"));
    QVERIFY(!renameTexture(textures, "c", "bad/name"));
    QVERIFY(!renameTexture(textures, "zzz", "y"));
}

QTEST_APPLESS_MAIN(TestTextureEdit)
#include "tst_texture_edit.moc"
