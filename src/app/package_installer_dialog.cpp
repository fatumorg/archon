#include "app/package_installer_dialog.h"

#include "app/busy.h"
#include "app/style.h"

#include <QButtonGroup>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace archon {

namespace {

constexpr int iconSize = 96;

QString text(const std::string &value)
{
    return QString::fromStdString(value);
}

QTextBrowser *page(const std::string &content, QWidget *parent)
{
    auto *browser = new QTextBrowser(parent);
    browser->setOpenExternalLinks(true);
    browser->setPlainText(text(content));
    return browser;
}

void colorText(QLabel *label, const QColor &color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

}

PackageInstallerDialog::PackageInstallerDialog(const std::filesystem::path &package, std::filesystem::path root, std::shared_ptr<const Keys> keys, std::filesystem::path store, QWidget *parent)
    : QDialog(parent)
    , root_(std::move(root))
    , keys_(std::move(keys))
    , store_(std::move(store))
{
    setWindowTitle(tr("Package Installer"));
    setWindowIcon(loadIcon(QStringLiteral("package")));
    resize(680, 520);

    QFile file(package);
    if (!file.open(QIODevice::ReadOnly)) {
        error_ = tr("Cannot read %1.").arg(QString::fromStdString(package.string()));
        return;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(file.size()));
    if (file.read(reinterpret_cast<char *>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size())) {
        error_ = tr("Cannot read %1.").arg(QString::fromStdString(package.string()));
        return;
    }
    auto zip = ZipArchive::open(std::move(bytes));
    if (!zip) {
        error_ = tr("This file is not an OIV package: %1").arg(text(zip.error().message));
        return;
    }
    auto read = readOivPackage(*zip);
    if (!read) {
        error_ = text(read.error().message);
        return;
    }
    zip_ = std::move(*zip);
    package_ = std::move(*read);

    steps_ = new QStackedWidget(this);
    steps_->setObjectName(QStringLiteral("installerSteps"));
    steps_->addWidget(buildDescriptionStep());
    steps_->addWidget(buildTargetStep());
    steps_->addWidget(buildResultStep());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildHeader());
    layout->addWidget(steps_, 1);
}

bool PackageInstallerDialog::isValid() const
{
    return package_.has_value();
}

QString PackageInstallerDialog::errorText() const
{
    return error_;
}

QWidget *PackageInstallerDialog::buildHeader()
{
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("packageHeader"));
    header->setAutoFillBackground(true);
    QPalette palette = header->palette();
    palette.setColor(QPalette::Window, QColor::fromRgba(package_->headerColor));
    header->setPalette(palette);
    header->setStyleSheet(QStringLiteral("#packageHeader { background: %1; }").arg(QColor::fromRgba(package_->headerColor).name()));

    auto *icon = new QLabel(header);
    icon->setFixedSize(iconSize + 16, iconSize + 16);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QStringLiteral("background: %1;").arg(QColor::fromRgba(package_->iconColor).name()));
    QPixmap pixmap;
    if (!package_->icon.empty() && pixmap.loadFromData(reinterpret_cast<const uchar *>(package_->icon.data()), static_cast<uint>(package_->icon.size())))
        icon->setPixmap(pixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        icon->setPixmap(loadIcon(QStringLiteral("package")).pixmap(iconSize, iconSize));

    const QColor foreground = package_->blackHeaderText ? QColor(Qt::black) : QColor(Qt::white);
    auto *name = new QLabel(text(package_->name), header);
    name->setObjectName(QStringLiteral("packageName"));
    QString version = package_->version.starts_with('1') ? QString() : tr("Version %1.%2").arg(package_->major).arg(package_->minor);
    if (!package_->tag.empty())
        version += QLatin1Char(' ') + text(package_->tag);
    auto *versionLabel = new QLabel(version, header);
    versionLabel->setObjectName(QStringLiteral("packageVersion"));
    auto *author = new QLabel(tr("by %1").arg(text(package_->author)), header);
    author->setObjectName(QStringLiteral("packageAuthor"));
    for (QLabel *label : { name, versionLabel, author }) {
        colorText(label, foreground);
        label->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(foreground.name()));
    }
    name->setStyleSheet(name->styleSheet() + QStringLiteral(" font-size: 17pt;"));

    auto *texts = new QVBoxLayout;
    texts->addStretch(1);
    texts->addWidget(name);
    texts->addWidget(versionLabel);
    texts->addWidget(author);
    texts->addStretch(1);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 16, 0);
    layout->setSpacing(16);
    layout->addWidget(icon);
    layout->addLayout(texts, 1);
    return header;
}

QWidget *PackageInstallerDialog::buildDescriptionStep()
{
    auto *step = new QWidget(this);
    auto *pages = new QTabWidget(step);
    pages->setObjectName(QStringLiteral("packagePages"));
    pages->addTab(page(package_->description, pages), tr("Description"));
    if (!package_->largeDescription.empty())
        pages->addTab(page(package_->largeDescription, pages), package_->largeDescriptionTitle.empty() ? tr("More") : text(package_->largeDescriptionTitle));
    if (!package_->licence.empty())
        pages->addTab(page(package_->licence, pages), tr("Licence"));

    auto *layout = new QVBoxLayout(step);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addWidget(pages, 1);

    contents_ = new QButtonGroup(step);
    if (package_->contents.size() > 1) {
        auto *options = new QGroupBox(tr("Installation options"), step);
        auto *optionsLayout = new QVBoxLayout(options);
        for (std::size_t index = 0; index < package_->contents.size(); ++index) {
            const OivContent &content = package_->contents[index];
            const QString caption = content.description.empty() ? text(content.name) : text(content.name) + QStringLiteral(" - ") + text(content.description);
            auto *option = new QRadioButton(caption, options);
            option->setObjectName(QStringLiteral("content%1").arg(index));
            option->setChecked(index == 0);
            contents_->addButton(option, static_cast<int>(index));
            optionsLayout->addWidget(option);
        }
        layout->addWidget(options);
    }

    auto *install = new QPushButton(tr("Install"), step);
    install->setObjectName(QStringLiteral("installButton"));
    install->setDefault(true);
    auto *close = new QPushButton(tr("Close"), step);
    connect(install, &QPushButton::clicked, this, [this] { steps_->setCurrentIndex(1); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(install);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    return step;
}

QWidget *PackageInstallerDialog::buildTargetStep()
{
    auto *step = new QWidget(this);
    auto *question = new QLabel(tr("Where do you want to install this package?"), step);
    QFont bold = question->font();
    bold.setBold(true);
    question->setFont(bold);

    auto *mods = new QPushButton(tr("Install to \"mods\" folder"), step);
    mods->setObjectName(QStringLiteral("modsTargetButton"));
    mods->setDefault(true);
    mods->setMinimumHeight(40);
    auto *modsNote = new QLabel(tr("Recommended. Archives the package changes are copied into the \"mods\" folder first; the original game files stay untouched. Needs the mods folder loader from Tools > ASI Manager."), step);
    modsNote->setWordWrap(true);
    auto *game = new QPushButton(tr("Install to game folder"), step);
    game->setObjectName(QStringLiteral("gameTargetButton"));
    game->setMinimumHeight(40);
    auto *gameNote = new QLabel(tr("Changes the game files themselves. Every change is backed up and can be rolled back from Tools > Change history."), step);
    gameNote->setWordWrap(true);
    connect(mods, &QPushButton::clicked, this, [this] { install(InstallTarget::Mods); });
    connect(game, &QPushButton::clicked, this, [this] { install(InstallTarget::Game); });

    auto *back = new QPushButton(tr("Back"), step);
    connect(back, &QPushButton::clicked, this, [this] { steps_->setCurrentIndex(0); });
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(back);

    auto *layout = new QVBoxLayout(step);
    layout->setContentsMargins(24, 20, 24, 12);
    layout->setSpacing(10);
    layout->addWidget(question);
    layout->addWidget(mods);
    layout->addWidget(modsNote);
    layout->addSpacing(8);
    layout->addWidget(game);
    layout->addWidget(gameNote);
    layout->addStretch(1);
    layout->addLayout(buttons);
    return step;
}

QWidget *PackageInstallerDialog::buildResultStep()
{
    auto *step = new QWidget(this);
    result_ = new QLabel(step);
    result_->setObjectName(QStringLiteral("resultText"));
    result_->setWordWrap(true);
    result_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *close = new QPushButton(tr("Close"), step);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(close);
    auto *layout = new QVBoxLayout(step);
    layout->setContentsMargins(24, 20, 24, 12);
    layout->addWidget(result_);
    layout->addStretch(1);
    layout->addLayout(buttons);
    return step;
}

void PackageInstallerDialog::install(InstallTarget target)
{
    if (!package_)
        return;
    const std::size_t content = contents_->checkedId() < 0 ? 0 : static_cast<std::size_t>(contents_->checkedId());
    Result<void> result;
    runBusy(this, tr("Installing %1…").arg(text(package_->name)), [&] {
        result = installOivPackage(*zip_, *package_, content, target, root_, keys_, store_);
    });
    result_->setText(result ? tr("Package installed.") : tr("The package was not installed. Nothing was changed.\n\n%1").arg(text(result.error().message)));
    steps_->setCurrentIndex(2);
}

}
