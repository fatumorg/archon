#include "app/texture_viewer.h"

#include "app/style.h"
#include "core/ytd/texture_edit.h"
#include "core/ytd/texture_format.h"
#include "core/ytd/texture_pixels.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace archon {

namespace {

constexpr int thumbnail = 64;
constexpr qint64 importLimit = 1ll << 30;
constexpr int maxImageSide = 16384;

QImage imageOf(const Texture &texture)
{
    const auto rgba = decodeRgba(texture, 0);
    if (!rgba)
        return {};
    QImage image(reinterpret_cast<const uchar *>(rgba->data()), static_cast<int>(texture.width), static_cast<int>(texture.height), QImage::Format_RGBA8888);
    return image.copy();
}

QString formatName(std::uint32_t code)
{
    const auto info = textureFormat(code);
    return info ? QString::fromLatin1(info->name.data(), static_cast<qsizetype>(info->name.size())) : QString::number(code);
}

}

TextureViewer::TextureViewer(const QString &title, std::vector<Texture> textures, bool editable, Save save, std::uint32_t version, QWidget *parent)
    : QDialog(parent)
    , textures_(std::move(textures))
    , editable_(editable)
    , version_(version)
    , save_(std::move(save))
{
    setWindowTitle(tr("%1 - Texture Dictionary").arg(title));
    setWindowIcon(loadIcon(QStringLiteral("file")));
    resize(900, 600);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("textureList"));
    list_->setIconSize(QSize(thumbnail, thumbnail));
    list_->setUniformItemSizes(true);
    connect(list_, &QListWidget::currentRowChanged, this, &TextureViewer::showSelected);

    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("texturePreview"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setStyleSheet(QStringLiteral("background: #808080;"));
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(preview_);
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignCenter);
    info_ = new QLabel(this);
    info_->setObjectName(QStringLiteral("textureInfo"));

    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(scroll, 1);
    rightLayout->addWidget(info_);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(list_);
    splitter->addWidget(right);
    splitter->setSizes({ 260, 640 });

    const auto makeButton = [this](const QString &text, const char *name) {
        auto *button = new QPushButton(text, this);
        button->setObjectName(QString::fromLatin1(name));
        return button;
    };
    export_ = makeButton(tr("Export…"), "exportButton");
    exportAll_ = makeButton(tr("Export all…"), "exportAllButton");
    add_ = makeButton(tr("Add…"), "addButton");
    replace_ = makeButton(tr("Replace…"), "replaceButton");
    rename_ = makeButton(tr("Rename…"), "renameButton");
    remove_ = makeButton(tr("Remove"), "removeButton");
    saveButton_ = makeButton(tr("Save"), "saveButton");
    auto *close = makeButton(tr("Close"), "closeButton");

    connect(export_, &QPushButton::clicked, this, [this] {
        const Texture *texture = selected();
        if (texture == nullptr)
            return;
        const QString file = QFileDialog::getSaveFileName(this, tr("Export texture"), QString::fromStdString(texture->name) + QStringLiteral(".dds"), tr("DDS texture (*.dds);;PNG image (*.png)"));
        if (!file.isEmpty())
            exportSelected(file);
    });
    connect(exportAll_, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Export all textures"));
        if (folder.isEmpty())
            return;
        const bool png = QMessageBox::question(this, QStringLiteral("ARCHON"), tr("Export as PNG images? Choose No for DDS textures.")) == QMessageBox::Yes;
        exportAll(folder, png);
    });
    connect(add_, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add textures"), QString(), tr("Textures (*.dds *.png *.jpg *.jpeg *.bmp *.tga)"));
        if (!files.isEmpty())
            addFiles(files);
    });
    connect(replace_, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Replace texture"), QString(), tr("Textures (*.dds *.png *.jpg *.jpeg *.bmp *.tga)"));
        if (!file.isEmpty())
            replaceSelected(file);
    });
    connect(rename_, &QPushButton::clicked, this, [this] {
        const Texture *texture = selected();
        if (texture == nullptr)
            return;
        const QString name = QInputDialog::getText(this, tr("Rename texture"), tr("New name:"), QLineEdit::Normal, QString::fromStdString(texture->name));
        if (!name.isEmpty())
            renameSelected(name);
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        const Texture *texture = selected();
        if (texture != nullptr && QMessageBox::question(this, QStringLiteral("ARCHON"), tr("Remove %1?").arg(QString::fromStdString(texture->name))) == QMessageBox::Yes)
            removeSelected();
    });
    connect(saveButton_, &QPushButton::clicked, this, &TextureViewer::save);
    connect(close, &QPushButton::clicked, this, &TextureViewer::reject);

    auto *buttons = new QHBoxLayout;
    for (QPushButton *button : { export_, exportAll_, add_, replace_, rename_, remove_ })
        buttons->addWidget(button);
    buttons->addStretch(1);
    buttons->addWidget(saveButton_);
    buttons->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addLayout(buttons);
    fill(0);
}

void TextureViewer::fill(int row)
{
    list_->clear();
    for (const Texture &texture : textures_) {
        auto *item = new QListWidgetItem(QStringLiteral("%1\n%2 x %3, %4").arg(QString::fromStdString(texture.name)).arg(texture.width).arg(texture.height).arg(formatName(texture.format)), list_);
        const QImage image = imageOf(texture);
        item->setIcon(image.isNull() ? loadIcon(QStringLiteral("file")) : QIcon(QPixmap::fromImage(image.scaled(thumbnail, thumbnail, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
    }
    list_->setCurrentRow(textures_.empty() ? -1 : std::clamp(row, 0, static_cast<int>(textures_.size()) - 1));
    showSelected();
}

const Texture *TextureViewer::selected() const
{
    const int row = list_->currentRow();
    return row < 0 || row >= static_cast<int>(textures_.size()) ? nullptr : &textures_[static_cast<std::size_t>(row)];
}

void TextureViewer::showSelected()
{
    const Texture *texture = selected();
    if (texture == nullptr) {
        preview_->clear();
        info_->clear();
    } else {
        const QImage image = imageOf(*texture);
        preview_->setPixmap(QPixmap::fromImage(image));
        if (texture->levels != 1)
            info_->setText(tr("%1: %2 x %3, %4, %5 mip levels").arg(QString::fromStdString(texture->name)).arg(texture->width).arg(texture->height).arg(formatName(texture->format)).arg(texture->levels));
        else
            info_->setText(tr("%1: %2 x %3, %4, 1 mip level").arg(QString::fromStdString(texture->name)).arg(texture->width).arg(texture->height).arg(formatName(texture->format)));
    }
    updateButtons();
}

void TextureViewer::updateButtons()
{
    const bool any = selected() != nullptr;
    export_->setEnabled(any);
    exportAll_->setEnabled(!textures_.empty());
    add_->setEnabled(editable_);
    replace_->setEnabled(editable_ && any);
    rename_->setEnabled(editable_ && any);
    remove_->setEnabled(editable_ && any);
    saveButton_->setEnabled(editable_ && modified_);
}

bool TextureViewer::warnIfFailed(const Result<void> &result)
{
    if (!result && !result.error().message.empty())
        QMessageBox::warning(this, QStringLiteral("ARCHON"), QString::fromStdString(result.error().message));
    return result.has_value();
}

bool TextureViewer::exportTexture(const Texture &texture, const QString &file)
{
    if (file.endsWith(QLatin1StringView(".png"), Qt::CaseInsensitive)) {
        const QImage image = imageOf(texture);
        if (image.isNull() || !image.save(file, "PNG"))
            return warnIfFailed(std::unexpected(Error { "export", 0, "cannot write " + file.toStdString() }));
        return true;
    }
    const auto dds = writeDds(texture);
    if (!dds)
        return warnIfFailed(std::unexpected(dds.error()));
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(reinterpret_cast<const char *>(dds->data()), static_cast<qint64>(dds->size())) != static_cast<qint64>(dds->size()))
        return warnIfFailed(std::unexpected(Error { "export", 0, "cannot write " + file.toStdString() }));
    return true;
}

bool TextureViewer::exportSelected(const QString &file)
{
    const Texture *texture = selected();
    return texture != nullptr && exportTexture(*texture, file);
}

bool TextureViewer::exportAll(const QString &folder, bool png)
{
    if (!QDir().mkpath(folder))
        return warnIfFailed(std::unexpected(Error { "export", 0, "cannot create " + folder.toStdString() }));
    for (const Texture &texture : textures_) {
        const QString name = QString::fromStdString(texture.name);
        if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name == QStringLiteral("..") || name == QStringLiteral("."))
            return warnIfFailed(std::unexpected(Error { "export", 0, texture.name + " is not a safe file name" }));
        if (!exportTexture(texture, QDir(folder).filePath(QString::fromStdString(texture.name) + (png ? QStringLiteral(".png") : QStringLiteral(".dds")))))
            return false;
    }
    return true;
}

Result<Texture> TextureViewer::load(const QString &file) const
{
    const std::string name = QFileInfo(file).completeBaseName().toStdString();
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly) || in.size() > importLimit)
        return std::unexpected(Error { "import", 0, "cannot read " + file.toStdString() });
    const QByteArray content = in.readAll();
    if (file.endsWith(QLatin1StringView(".dds"), Qt::CaseInsensitive))
        return readDds(std::as_bytes(std::span(content.constData(), static_cast<std::size_t>(content.size()))), name);
    QImage image;
    if (!image.loadFromData(content))
        return std::unexpected(Error { "import", 0, file.toStdString() + " is not an image ARCHON can read" });
    if (image.width() > maxImageSide || image.height() > maxImageSide)
        return std::unexpected(Error { "import", 0, file.toStdString() + " is larger than 16384 pixels" });
    image = image.convertToFormat(QImage::Format_RGBA8888);
    std::vector<std::byte> rgba(static_cast<std::size_t>(image.width()) * image.height() * 4);
    for (int y = 0; y < image.height(); ++y) {
        const auto line = std::as_bytes(std::span(image.constScanLine(y), static_cast<std::size_t>(image.width()) * 4));
        std::ranges::copy(line, rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * image.width() * 4));
    }
    return rgbaTexture(name, static_cast<std::uint32_t>(image.width()), static_cast<std::uint32_t>(image.height()), rgba);
}

void TextureViewer::addFiles(const QStringList &files)
{
    if (!editable_)
        return;
    for (const QString &file : files) {
        auto texture = load(file);
        if (!texture) {
            warnIfFailed(std::unexpected(texture.error()));
            continue;
        }
        if (!warnIfFailed(addTexture(textures_, std::move(*texture))))
            continue;
        modified_ = true;
    }
    fill(static_cast<int>(textures_.size()) - 1);
}

void TextureViewer::replaceSelected(const QString &file)
{
    const Texture *texture = selected();
    if (!editable_ || texture == nullptr)
        return;
    auto replacement = load(file);
    if (!replacement) {
        warnIfFailed(std::unexpected(replacement.error()));
        return;
    }
    const int row = list_->currentRow();
    if (warnIfFailed(replaceTexture(textures_, texture->name, std::move(*replacement)))) {
        modified_ = true;
        fill(row);
    }
}

void TextureViewer::renameSelected(const QString &name)
{
    const Texture *texture = selected();
    if (!editable_ || texture == nullptr)
        return;
    const int row = list_->currentRow();
    if (warnIfFailed(renameTexture(textures_, texture->name, name.toStdString()))) {
        modified_ = true;
        fill(row);
    }
}

void TextureViewer::removeSelected()
{
    const Texture *texture = selected();
    if (!editable_ || texture == nullptr)
        return;
    const int row = list_->currentRow();
    if (warnIfFailed(removeTexture(textures_, texture->name))) {
        modified_ = true;
        fill(row);
    }
}

bool TextureViewer::save()
{
    if (!editable_ || !modified_)
        return false;
    const auto file = writeTextureDictionary(textures_, version_);
    if (!file)
        return warnIfFailed(std::unexpected(file.error()));
    if (!warnIfFailed(save_(*file)))
        return false;
    modified_ = false;
    updateButtons();
    return true;
}

void TextureViewer::reject()
{
    if (modified_) {
        const auto answer = QMessageBox::question(this, QStringLiteral("ARCHON"), tr("Save the changes to the textures?"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save()))
            return;
    }
    QDialog::reject();
}

}
