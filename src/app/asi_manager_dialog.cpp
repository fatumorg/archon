#include "app/asi_manager_dialog.h"

#include "app/busy.h"
#include "app/download.h"
#include "app/prefix_setup_dialog.h"
#include "app/style.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace archon {

namespace {

constexpr qint64 downloadLimit = 64 << 20;
constexpr int pathRole = Qt::UserRole;

QString text(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::span<const std::byte> bytesOf(const QByteArray &content)
{
    return std::as_bytes(std::span(content.constData(), static_cast<std::size_t>(content.size())));
}

QLabel *titleLabel(const QString &title, QWidget *parent)
{
    auto *label = new QLabel(title, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

}

AsiManagerDialog::AsiManagerDialog(std::filesystem::path root, GameEdition edition, std::filesystem::path store, QWidget *parent)
    : QDialog(parent)
    , root_(std::move(root))
    , edition_(edition)
    , store_(std::move(store))
{
    setWindowTitle(tr("ASI Manager"));
    setWindowIcon(loadIcon(QStringLiteral("asi")));
    resize(640, 520);

    auto *loaders = new QGroupBox(tr("Loaders"), this);
    auto *grid = new QGridLayout(loaders);
    loaderStatus_ = new QLabel(loaders);
    loaderStatus_->setObjectName(QStringLiteral("loaderStatus"));
    modsLoaderStatus_ = new QLabel(loaders);
    modsLoaderStatus_->setObjectName(QStringLiteral("modsLoaderStatus"));
    loaderButton_ = new QPushButton(loaders);
    loaderButton_->setObjectName(QStringLiteral("loaderButton"));
    modsLoaderButton_ = new QPushButton(loaders);
    modsLoaderButton_->setObjectName(QStringLiteral("modsLoaderButton"));
    auto *loaderIcon = new QLabel(loaders);
    loaderIcon->setPixmap(loadIcon(QStringLiteral("asi")).pixmap(32, 32));
    auto *modsIcon = new QLabel(loaders);
    modsIcon->setPixmap(loadIcon(QStringLiteral("folder")).pixmap(32, 32));
    grid->addWidget(loaderIcon, 0, 0, 2, 1);
    grid->addWidget(titleLabel(tr("ASI Loader"), loaders), 0, 1);
    grid->addWidget(new QLabel(tr("Loads .asi plugins (Ultimate ASI Loader, installed as %1).").arg(text(loaderFileName(edition_))), loaders), 1, 1);
    grid->addWidget(loaderStatus_, 0, 2);
    grid->addWidget(loaderButton_, 0, 3, 2, 1);
    grid->addWidget(modsIcon, 2, 0, 2, 1);
    grid->addWidget(titleLabel(tr("Mods folder loader (RageOpenV.asi)"), loaders), 2, 1);
    grid->addWidget(new QLabel(tr("Makes the game load archives from the \"mods\" folder. Open-source replacement for OpenIV.ASI."), loaders), 3, 1);
    grid->addWidget(modsLoaderStatus_, 2, 2);
    grid->addWidget(modsLoaderButton_, 2, 3, 2, 1);
    grid->setColumnStretch(1, 1);
    const int statusWidth = loaderStatus_->fontMetrics().horizontalAdvance(tr("Installed (Ultimate ASI Loader v10.0.2)")) + 8;
    loaderStatus_->setMinimumWidth(statusWidth);
    modsLoaderStatus_->setMinimumWidth(statusWidth);
    loaderButton_->setText(tr("Uninstall"));
    const int buttonWidth = loaderButton_->sizeHint().width();
    loaderButton_->setMinimumWidth(buttonWidth);
    modsLoaderButton_->setMinimumWidth(buttonWidth);
    connect(loaderButton_, &QPushButton::clicked, this, [this] {
        if (!state_.loaderInstalled) {
            install(asiLoaderDownload());
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("ARCHON"), tr("Uninstall the ASI Loader?")) == QMessageBox::Yes)
            apply(tr("Uninstalling…"), [](AsiManager &manager) { return manager.uninstallLoader(); });
    });
    connect(modsLoaderButton_, &QPushButton::clicked, this, [this] {
        if (state_.modsLoader.empty()) {
            install(modsLoaderDownload());
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("ARCHON"), tr("Uninstall the mods folder loader? The game will load its original archives again.")) == QMessageBox::Yes)
            apply(tr("Uninstalling…"), [](AsiManager &manager) { return manager.uninstallModsLoader(); });
    });

    auto *pluginBox = new QGroupBox(tr("ASI plugins"), this);
    plugins_ = new QTreeWidget(pluginBox);
    plugins_->setObjectName(QStringLiteral("pluginList"));
    plugins_->setHeaderLabels({ tr("Name"), tr("Location"), tr("State") });
    plugins_->setRootIsDecorated(false);
    plugins_->setUniformRowHeights(true);
    plugins_->header()->resizeSection(0, 260);
    plugins_->header()->resizeSection(1, 140);
    connect(plugins_, &QTreeWidget::currentItemChanged, this, &AsiManagerDialog::updateButtons);
    auto *add = new QPushButton(tr("Add…"), pluginBox);
    add->setObjectName(QStringLiteral("addButton"));
    remove_ = new QPushButton(tr("Remove"), pluginBox);
    remove_->setObjectName(QStringLiteral("removeButton"));
    toggle_ = new QPushButton(tr("Disable"), pluginBox);
    toggle_->setObjectName(QStringLiteral("toggleButton"));
    connect(add, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add ASI plugins"), QString(), tr("ASI plugins (*.asi)"));
        if (!files.isEmpty())
            addPlugins(files);
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        const QString path = selectedPlugin();
        if (path.isEmpty() || QMessageBox::question(this, QStringLiteral("ARCHON"), tr("Remove %1?").arg(plugins_->currentItem()->text(0))) != QMessageBox::Yes)
            return;
        apply(tr("Removing…"), [path](AsiManager &manager) { return manager.removePlugin(path.toStdString()); });
    });
    connect(toggle_, &QPushButton::clicked, this, [this] {
        const QString path = selectedPlugin();
        if (path.isEmpty())
            return;
        const bool enable = path.endsWith(QLatin1StringView(".disabled"), Qt::CaseInsensitive);
        apply(enable ? tr("Enabling…") : tr("Disabling…"), [path, enable](AsiManager &manager) { return manager.setPluginEnabled(path.toStdString(), enable); });
    });
    auto *pluginButtons = new QVBoxLayout;
    pluginButtons->addWidget(add);
    pluginButtons->addWidget(remove_);
    pluginButtons->addWidget(toggle_);
    pluginButtons->addStretch(1);
    auto *pluginLayout = new QHBoxLayout(pluginBox);
    pluginLayout->addWidget(plugins_, 1);
    pluginLayout->addLayout(pluginButtons);

    auto *buttons = new QDialogButtonBox(this);
#ifdef Q_OS_LINUX
    auto *prefixButton = buttons->addButton(tr("Prefix setup…"), QDialogButtonBox::ActionRole);
    prefixButton->setObjectName(QStringLiteral("prefixButton"));
    const auto prefix = findPrefix(root_, edition_);
    prefixButton->setEnabled(prefix.has_value());
    if (!prefix)
        prefixButton->setToolTip(tr("No Proton prefix was found for this game. Start the game once through Steam to create it."));
    connect(prefixButton, &QPushButton::clicked, this, [this, prefix] {
        PrefixSetupDialog dialog(*prefix, edition_, store_ / ("prefix-" + std::to_string(steamAppId(edition_)) + ".log"), "/proc", this);
        dialog.exec();
    });
#endif
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(loaders);
    layout->addWidget(pluginBox, 1);
    layout->addWidget(buttons);
    refresh();
}

void AsiManagerDialog::refresh()
{
    const QString current = selectedPlugin();
    const QString currentBase = current.endsWith(QLatin1StringView(".disabled")) ? current.chopped(9) : current;
    state_ = AsiManager(root_, edition_, store_).state();
    loaderStatus_->setText(!state_.loaderInstalled ? tr("Not installed") : state_.loaderPinned ? tr("Installed (Ultimate ASI Loader v10.0.2)") : tr("Installed (other version)"));
    loaderButton_->setText(state_.loaderInstalled ? tr("Uninstall") : tr("Install"));
    modsLoaderStatus_->setText(state_.modsLoader.empty() ? tr("Not installed") : tr("Installed (%1)").arg(QString::fromStdString(state_.modsLoader)));
    modsLoaderButton_->setText(state_.modsLoader.empty() ? tr("Install") : tr("Uninstall"));
    plugins_->clear();
    for (const AsiPlugin &plugin : state_.plugins) {
        const QString path = QString::fromStdString(plugin.path);
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        QString name = path.mid(slash + 1);
        if (!plugin.enabled)
            name.chop(9);
        auto *item = new QTreeWidgetItem(plugins_, { name, slash < 0 ? tr("game folder") : path.left(slash), plugin.enabled ? tr("Enabled") : tr("Disabled") });
        item->setData(0, pathRole, path);
        const QString base = plugin.enabled ? path : path.chopped(9);
        if (!currentBase.isEmpty() && base == currentBase)
            plugins_->setCurrentItem(item);
    }
    updateButtons();
}

void AsiManagerDialog::updateButtons()
{
    const QString path = selectedPlugin();
    remove_->setEnabled(!path.isEmpty());
    toggle_->setEnabled(!path.isEmpty());
    toggle_->setText(path.endsWith(QLatin1StringView(".disabled"), Qt::CaseInsensitive) ? tr("Enable") : tr("Disable"));
}

QString AsiManagerDialog::selectedPlugin() const
{
    const QTreeWidgetItem *item = plugins_->currentItem();
    return item == nullptr ? QString() : item->data(0, pathRole).toString();
}

bool AsiManagerDialog::apply(const QString &label, const std::function<Result<void>(AsiManager &)> &work)
{
    Result<void> result;
    runBusy(this, label, [&] {
        AsiManager manager(root_, edition_, store_);
        result = work(manager);
    });
    refresh();
    if (!result)
        QMessageBox::warning(this, QStringLiteral("ARCHON"), QString::fromStdString(result.error().message));
    return result.has_value();
}

void AsiManagerDialog::installFrom(const PinnedDownload &which, const QByteArray &bytes)
{
    auto content = pinnedContent(which, bytesOf(bytes));
    if (!content) {
        QMessageBox::warning(this, QStringLiteral("ARCHON"), QString::fromStdString(content.error().message));
        return;
    }
    const bool loader = &which == &asiLoaderDownload();
    apply(tr("Installing…"), [&content, loader](AsiManager &manager) { return loader ? manager.installLoader(*content) : manager.installModsLoader(*content); });
}

void AsiManagerDialog::install(const PinnedDownload &which)
{
    const QString title = text(which.title);
    auto bytes = download(this, tr("Downloading %1…").arg(title), QUrl(text(which.url)), downloadLimit);
    if (!bytes) {
        const auto answer = QMessageBox::question(this, QStringLiteral("ARCHON"),
            tr("%1 could not be downloaded:\n%2\n\nChoose a local copy instead (%3 or %4)?").arg(title, QString::fromStdString(bytes.error().message), QUrl(text(which.url)).fileName(), text(which.member)));
        if (answer != QMessageBox::Yes)
            return;
        const QString fileName = QFileDialog::getOpenFileName(this, title);
        if (fileName.isEmpty())
            return;
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly) || file.size() > downloadLimit) {
            QMessageBox::warning(this, QStringLiteral("ARCHON"), tr("Cannot read %1.").arg(fileName));
            return;
        }
        bytes = file.readAll();
    }
    installFrom(which, *bytes);
}

void AsiManagerDialog::addPlugins(const QStringList &files)
{
    std::vector<std::filesystem::path> sources;
    for (const QString &file : files)
        sources.emplace_back(file.toStdString());
    apply(tr("Adding plugins…"), [&sources](AsiManager &manager) { return manager.addPlugins(sources); });
}

}
