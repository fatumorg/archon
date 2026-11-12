#include "app/package_creator_dialog.h"

#include "app/busy.h"
#include "app/style.h"

#include <QBuffer>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace archon {

namespace {

constexpr int iconSide = 128;

std::string text(const QLineEdit *edit)
{
    return edit->text().trimmed().toStdString();
}

std::string text(const QPlainTextEdit *edit)
{
    return edit->toPlainText().trimmed().toStdString();
}

}

PackageCreatorDialog::PackageCreatorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Package Creator"));
    setWindowIcon(loadIcon(QStringLiteral("package")));
    resize(760, 600);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildPackageTab(), tr("Package"));
    tabs->addTab(buildContentTab(), tr("Content"));

    auto *buttons = new QDialogButtonBox(this);
    auto *create = buttons->addButton(tr("Create package…"), QDialogButtonBox::AcceptRole);
    create->setObjectName(QStringLiteral("createButton"));
    buttons->addButton(QDialogButtonBox::Close);
    connect(create, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getSaveFileName(this, tr("Create package"), name_->text().trimmed() + QStringLiteral(".oiv"), tr("OIV packages (*.oiv)"));
        if (!file.isEmpty() && createPackage(file))
            QMessageBox::information(this, QStringLiteral("ARCHON"), tr("Package created: %1").arg(file));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);
}

QWidget *PackageCreatorDialog::buildPackageTab()
{
    auto *tab = new QWidget(this);
    const auto line = [tab](const char *name) {
        auto *edit = new QLineEdit(tab);
        edit->setObjectName(QString::fromLatin1(name));
        return edit;
    };
    const auto block = [tab](const char *name) {
        auto *edit = new QPlainTextEdit(tab);
        edit->setObjectName(QString::fromLatin1(name));
        edit->setTabChangesFocus(true);
        return edit;
    };
    name_ = line("packageName");
    tag_ = line("versionTag");
    author_ = line("authorName");
    link_ = line("authorLink");
    historyTitle_ = line("historyTitle");
    historyTitle_->setText(tr("Version history"));
    description_ = block("description");
    history_ = block("history");
    licence_ = block("licence");
    major_ = new QSpinBox(tab);
    major_->setObjectName(QStringLiteral("versionMajor"));
    major_->setRange(0, 9999);
    major_->setValue(1);
    minor_ = new QSpinBox(tab);
    minor_->setObjectName(QStringLiteral("versionMinor"));
    minor_->setRange(0, 9999);
    blackText_ = new QCheckBox(tr("Black text in the header"), tab);
    blackText_->setObjectName(QStringLiteral("blackHeaderText"));
    headerButton_ = new QPushButton(tab);
    headerButton_->setObjectName(QStringLiteral("headerColorButton"));
    iconColorButton_ = new QPushButton(tab);
    iconColorButton_->setObjectName(QStringLiteral("iconColorButton"));
    showColor(headerButton_, headerColor_);
    showColor(iconColorButton_, iconColor_);
    connect(headerButton_, &QPushButton::clicked, this, [this] { pickColor(headerButton_, headerColor_); });
    connect(iconColorButton_, &QPushButton::clicked, this, [this] { pickColor(iconColorButton_, iconColor_); });
    iconPreview_ = new QLabel(tr("No icon"), tab);
    iconPreview_->setFixedSize(64, 64);
    iconPreview_->setAlignment(Qt::AlignCenter);
    auto *chooseIcon = new QPushButton(tr("Choose icon…"), tab);
    connect(chooseIcon, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Package icon"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (file.isEmpty())
            return;
        setIcon(file);
    });

    auto *version = new QHBoxLayout;
    version->addWidget(major_);
    version->addWidget(new QLabel(QStringLiteral("."), tab));
    version->addWidget(minor_);
    version->addWidget(tag_, 1);
    auto *colors = new QHBoxLayout;
    colors->addWidget(new QLabel(tr("Header"), tab));
    colors->addWidget(headerButton_);
    colors->addWidget(new QLabel(tr("Icon area"), tab));
    colors->addWidget(iconColorButton_);
    colors->addWidget(blackText_);
    colors->addStretch(1);
    auto *iconRow = new QHBoxLayout;
    iconRow->addWidget(iconPreview_);
    iconRow->addWidget(chooseIcon);
    iconRow->addStretch(1);

    auto *form = new QFormLayout(tab);
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Version"), version);
    form->addRow(tr("Author"), author_);
    form->addRow(tr("Author link"), link_);
    form->addRow(tr("Description"), description_);
    form->addRow(tr("Second page title"), historyTitle_);
    form->addRow(tr("Second page"), history_);
    form->addRow(tr("Licence"), licence_);
    form->addRow(tr("Colours"), colors);
    form->addRow(tr("Icon"), iconRow);
    return tab;
}

QWidget *PackageCreatorDialog::buildContentTab()
{
    auto *tab = new QWidget(this);
    table_ = new QTableWidget(0, 3, tab);
    table_->setObjectName(QStringLiteral("contentTable"));
    table_->setHorizontalHeaderLabels({ tr("Action"), tr("Source"), tr("Target in the game") });
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->resizeSection(0, 70);
    table_->horizontalHeader()->resizeSection(1, 220);

    auto *addFilesButton = new QPushButton(tr("Add files…"), tab);
    auto *deleteButton = new QPushButton(tr("Delete game file…"), tab);
    auto *removeButton = new QPushButton(tr("Remove"), tab);
    connect(addFilesButton, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Files to add"));
        if (files.isEmpty())
            return;
        bool ok = false;
        const QString target = QInputDialog::getText(this, tr("Target folder"), tr("Folder in the game, archives included (for example x64e.rpf/levels/gta5/vehicles.rpf). Empty for the game folder:"), QLineEdit::Normal, QString(), &ok);
        if (ok)
            addFiles(files, target);
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString target = QInputDialog::getText(this, tr("Delete game file"), tr("Path of the file in the game, archives included:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !target.trimmed().isEmpty())
            addDelete(target.trimmed());
    });
    connect(removeButton, &QPushButton::clicked, this, &PackageCreatorDialog::removeSelected);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(addFilesButton);
    buttons->addWidget(deleteButton);
    buttons->addWidget(removeButton);
    buttons->addStretch(1);
    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(table_, 1);
    layout->addLayout(buttons);
    return tab;
}

void PackageCreatorDialog::showColor(QPushButton *button, std::uint32_t color)
{
    const QColor shown = QColor::fromRgba(color);
    button->setText(shown.name(QColor::HexArgb).toUpper());
    button->setStyleSheet(QStringLiteral("background: %1; color: %2;").arg(shown.name(), shown.lightness() > 128 ? QStringLiteral("#000000") : QStringLiteral("#ffffff")));
}

void PackageCreatorDialog::pickColor(QPushButton *button, std::uint32_t &color)
{
    const QColor chosen = QColorDialog::getColor(QColor::fromRgba(color), this, tr("Choose a colour"));
    if (!chosen.isValid())
        return;
    color = chosen.rgba();
    showColor(button, color);
}

void PackageCreatorDialog::addRow(const PackageItem &item)
{
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(item.kind == PackageItem::Kind::Add ? tr("Add") : tr("Delete")));
    table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(item.source.string())));
    table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(item.target)));
}

void PackageCreatorDialog::addFiles(const QStringList &files, const QString &target)
{
    QString folder = target.trimmed();
    folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (folder.endsWith(QLatin1Char('/')))
        folder.chop(1);
    for (const QString &file : files) {
        const QString name = QFileInfo(file).fileName();
        PackageItem item { PackageItem::Kind::Add, file.toStdString(), (folder.isEmpty() ? name : folder + QLatin1Char('/') + name).toStdString() };
        items_.push_back(item);
        addRow(item);
    }
}

void PackageCreatorDialog::addDelete(const QString &target)
{
    PackageItem item { PackageItem::Kind::Delete, {}, target.toStdString() };
    items_.push_back(item);
    addRow(item);
}

void PackageCreatorDialog::removeSelected()
{
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(items_.size()))
        return;
    items_.erase(items_.begin() + row);
    table_->removeRow(row);
}

PackageDraft PackageCreatorDialog::draft() const
{
    return { text(name_), major_->value(), minor_->value(), text(tag_), text(author_), text(link_), text(description_), text(historyTitle_), text(history_), text(licence_), headerColor_, blackText_->isChecked(), iconColor_, icon_, items_ };
}

bool PackageCreatorDialog::setIcon(const QString &file)
{
    const QImage source(file);
    if (source.isNull()) {
        QMessageBox::warning(this, QStringLiteral("ARCHON"), tr("%1 is not an image ARCHON can read.").arg(file));
        return false;
    }
    const QImage scaled = source.scaled(iconSide, iconSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage icon(iconSide, iconSide, QImage::Format_ARGB32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.drawImage((iconSide - scaled.width()) / 2, (iconSide - scaled.height()) / 2, scaled);
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    icon.save(&buffer, "PNG");
    const auto bytes = std::as_bytes(std::span(png.constData(), static_cast<std::size_t>(png.size())));
    icon_.assign(bytes.begin(), bytes.end());
    iconPreview_->setPixmap(QPixmap::fromImage(icon).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    return true;
}

bool PackageCreatorDialog::createPackage(const QString &file)
{
    const PackageDraft current = draft();
    Result<void> result;
    runBusy(this, tr("Creating package…"), [&] { result = writeOivPackage(current, file.toStdString()); });
    if (!result) {
        QMessageBox::warning(this, QStringLiteral("ARCHON"), QString::fromStdString(result.error().message));
        return false;
    }
    return true;
}

}
