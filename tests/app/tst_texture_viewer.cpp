#include "app/texture_viewer.h"
#include "core/rsc/resource_file.h"
#include "core/rsc/rsc7.h"
#include "core/ytd/texture_format.h"
#include "core/ytd/texture_pixels.h"
#include "support/modal_driver.h"
#include "support/temp_files.h"

#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTest>

using namespace archon;
using namespace archon::test;

class TestTextureViewer : public QObject {
    Q_OBJECT
private slots:
    void init();
    void listsAndPreviews();
    void exportsDdsAndPng();
    void editingNeedsEditMode();
    void editsAndSaves();
    void hostileNamesAndFormatsAreNotExported();
    void hugeImagesAreRefused();
    void enhancedDictionaryIsSavedAsEnhanced();

private:
    std::unique_ptr<TextureViewer> viewer(bool editable)
    {
        auto made = std::make_unique<TextureViewer>(QStringLiteral("x.ytd"), textures_, editable, [this](const std::vector<std::byte> &bytes) -> Result<void> {
            saved_ = bytes;
            return {};
        });
        made->show();
        return made;
    }
    QPushButton *button(TextureViewer &shown, const char *name)
    {
        return shown.findChild<QPushButton *>(QString::fromLatin1(name));
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::vector<Texture> textures_;
    std::vector<std::byte> saved_;
};

void TestTextureViewer::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    saved_.clear();
    Texture blocks { "diffuse", 8, 8, 2, 0x31545844, 0x2002b054, std::vector<std::byte>(40) };
    for (std::size_t i = 0; i < 32; i += 8) {
        blocks.data[i] = std::byte { 0x00 };
        blocks.data[i + 1] = std::byte { 0xF8 };
    }
    textures_ = { blocks, rgbaTexture("normal", 4, 2, std::vector<std::byte>(32, std::byte { 200 })) };
}

void TestTextureViewer::listsAndPreviews()
{
    auto shown = viewer(false);
    auto *list = shown->findChild<QListWidget *>(QStringLiteral("textureList"));
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);
    QVERIFY(list->item(0)->text().startsWith(QStringLiteral("diffuse")));
    QVERIFY(list->item(0)->text().contains(QStringLiteral("8 x 8")));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("textureInfo"))->text(), QStringLiteral("diffuse: 8 x 8, DXT1, 2 mip levels"));
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("texturePreview"))->pixmap().size(), QSize(8, 8));
    list->setCurrentRow(1);
    QCOMPARE(shown->findChild<QLabel *>(QStringLiteral("textureInfo"))->text(), QStringLiteral("normal: 4 x 2, A8R8G8B8, 3 mip levels"));
    QCOMPARE(shown->windowTitle(), QStringLiteral("x.ytd - Texture Dictionary"));
}

void TestTextureViewer::exportsDdsAndPng()
{
    auto shown = viewer(false);
    const QString folder = directory_->path();
    QVERIFY(shown->exportSelected(folder + QStringLiteral("/one.dds")));
    const auto dds = readDds(readFile(pathOf(*directory_) / "one.dds"), "x");
    QVERIFY(dds);
    QVERIFY(dds->data == textures_[0].data);
    QVERIFY(shown->exportSelected(folder + QStringLiteral("/one.png")));
    const QImage png(folder + QStringLiteral("/one.png"));
    QCOMPARE(png.size(), QSize(8, 8));
    QCOMPARE(png.pixelColor(0, 0), QColor(255, 0, 0));
    QVERIFY(shown->exportAll(folder + QStringLiteral("/all"), true));
    QVERIFY(QImage(folder + QStringLiteral("/all/diffuse.png")).size() == QSize(8, 8));
    QVERIFY(QImage(folder + QStringLiteral("/all/normal.png")).size() == QSize(4, 2));
}

void TestTextureViewer::editingNeedsEditMode()
{
    auto shown = viewer(false);
    for (const char *name : { "addButton", "replaceButton", "renameButton", "removeButton", "saveButton" })
        QVERIFY2(!button(*shown, name)->isEnabled(), name);
    QVERIFY(button(*shown, "exportButton")->isEnabled());
    auto editable = viewer(true);
    for (const char *name : { "addButton", "replaceButton", "renameButton", "removeButton" })
        QVERIFY2(button(*editable, name)->isEnabled(), name);
    QVERIFY(!button(*editable, "saveButton")->isEnabled());
}

void TestTextureViewer::editsAndSaves()
{
    auto shown = viewer(true);
    const QString folder = directory_->path();
    QImage image(16, 8, QImage::Format_RGBA8888);
    image.fill(QColor(10, 20, 30));
    QVERIFY(image.save(folder + QStringLiteral("/added_one.png")));
    writeFile(pathOf(*directory_) / "replacement.dds", *writeDds(rgbaTexture("r", 2, 2, std::vector<std::byte>(16, std::byte { 5 }))));
    ModalDriver driver({});
    shown->addFiles({ folder + QStringLiteral("/added_one.png") });
    auto *list = shown->findChild<QListWidget *>(QStringLiteral("textureList"));
    QCOMPARE(list->count(), 3);
    QVERIFY(button(*shown, "saveButton")->isEnabled());
    list->setCurrentRow(0);
    shown->replaceSelected(folder + QStringLiteral("/replacement.dds"));
    shown->renameSelected(QStringLiteral("renamed"));
    list->setCurrentRow(1);
    shown->removeSelected();
    QVERIFY2(driver.unexpected.isEmpty(), qPrintable(driver.unexpected.join(QLatin1Char('\n'))));
    QVERIFY(shown->save());
    QVERIFY(!button(*shown, "saveButton")->isEnabled());
    const auto header = parseRsc7Header(std::span(saved_).first(16));
    QVERIFY(header);
    const auto pages = inflateResource(saved_);
    QVERIFY(pages);
    const auto textures = readTextureDictionary(*pages, rsc7SizeFromFlags(header->systemFlags));
    QVERIFY(textures);
    QStringList names;
    for (const Texture &texture : *textures)
        names.append(QString::fromStdString(texture.name));
    names.sort();
    QCOMPARE(names, QStringList({ "added_one", "renamed" }));
    const auto renamed = std::ranges::find(*textures, "renamed", &Texture::name);
    QCOMPARE(renamed->width, 2u);
    QCOMPARE(renamed->usage, 0x2002b054u);
}

void TestTextureViewer::hostileNamesAndFormatsAreNotExported()
{
    textures_ = { { "../escape", 4, 4, 1, 0x31545844, 0, std::vector<std::byte>(8) }, { "odd", 4, 4, 1, 12345, 0, {} } };
    auto shown = viewer(false);
    ModalDriver driver({ QStringLiteral("OK"), QStringLiteral("OK") });
    QVERIFY(!shown->exportAll(directory_->path() + QStringLiteral("/out/inner"), false));
    QVERIFY(!QFile::exists(directory_->path() + QStringLiteral("/out/escape.dds")));
    shown->findChild<QListWidget *>(QStringLiteral("textureList"))->setCurrentRow(1);
    QVERIFY(!shown->exportSelected(directory_->path() + QStringLiteral("/odd.dds")));
    QVERIFY(!QFile::exists(directory_->path() + QStringLiteral("/odd.dds")));
    QVERIFY(driver.done());
}

void TestTextureViewer::hugeImagesAreRefused()
{
    auto shown = viewer(true);
    QImage image(20000, 1, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    QVERIFY(image.save(directory_->path() + QStringLiteral("/huge.png")));
    ModalDriver driver({ QStringLiteral("OK") });
    shown->addFiles({ directory_->path() + QStringLiteral("/huge.png") });
    QVERIFY(driver.done());
    QVERIFY(driver.seen.first().contains(QStringLiteral("16384")));
    QCOMPARE(shown->findChild<QListWidget *>(QStringLiteral("textureList"))->count(), 2);
}

void TestTextureViewer::enhancedDictionaryIsSavedAsEnhanced()
{
    TextureViewer shown(QStringLiteral("e.ytd"), textures_, true, [this](const std::vector<std::byte> &bytes) -> Result<void> {
        saved_ = bytes;
        return {};
    }, enhancedDictionaryVersion);
    shown.renameSelected(QStringLiteral("renamed"));
    QVERIFY(shown.save());
    const auto header = parseRsc7Header(std::span(saved_).first(16));
    QVERIFY(header);
    QCOMPARE(header->version, 5u);
    const auto textures = readTextureDictionary(*inflateResource(saved_), rsc7SizeFromFlags(header->systemFlags), header->version);
    QVERIFY(textures);
    QCOMPARE(textures->size(), std::size_t(2));
}

QTEST_MAIN(TestTextureViewer)
#include "tst_texture_viewer.moc"
