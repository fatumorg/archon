#include "app/game_setup.h"

#include "core/crypto/hash.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace archon {

namespace {

const QLatin1StringView magicUrl("https://raw.githubusercontent.com/dexyfex/CodeWalker/master/CodeWalker.Core/Resources/magic.dat");
const QLatin1StringView magicDigest("dc35981f822e892ced3aa81d31e7a96927d573ee28f67417592b5afeaf330832");
const QLatin1StringView pathSetting("game/path");
constexpr qint64 magicSizeLimit = 16 << 20;
constexpr int downloadTimeoutMs = 30000;
constexpr int rootRole = Qt::UserRole;

std::span<const std::byte> bytesOf(const QByteArray &content)
{
    return std::as_bytes(std::span(content.constData(), static_cast<std::size_t>(content.size())));
}

QSettings settings()
{
    return QSettings(QStringLiteral("archon"), QStringLiteral("archon"));
}

QString describe(const GameInstall &install)
{
    return QStringLiteral("%1  (%2)").arg(QString::fromStdString(install.root.string()), install.edition == GameEdition::Legacy ? QStringLiteral("Legacy") : QStringLiteral("Enhanced"));
}

std::optional<GameInstall> askForInstall(QWidget *parent, const std::vector<GameInstall> &found)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Select game folder"));
    dialog.resize(820, 320);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QObject::tr("Choose the Grand Theft Auto V folder ARCHON should open."), &dialog));
    auto *list = new QListWidget(&dialog);
    layout->addWidget(list, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *browse = buttons->addButton(QObject::tr("Browse…"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    const auto add = [list](const GameInstall &install) {
        auto *item = new QListWidgetItem(describe(install), list);
        item->setData(rootRole, QString::fromStdString(install.root.string()));
        list->setCurrentItem(item);
    };
    for (const GameInstall &install : found)
        add(install);
    list->setCurrentRow(0);
    const auto refresh = [buttons, list] { buttons->button(QDialogButtonBox::Ok)->setEnabled(list->currentItem() != nullptr); };
    refresh();
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog, refresh);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, add] {
        const QString directory = QFileDialog::getExistingDirectory(&dialog, QObject::tr("Grand Theft Auto V folder"));
        if (directory.isEmpty())
            return;
        const auto install = openGameInstall(directory.toStdString());
        if (!install) {
            QMessageBox::warning(&dialog, QStringLiteral("ARCHON"), QObject::tr("That folder holds no GTA5.exe or GTA5_Enhanced.exe."));
            return;
        }
        add(*install);
    });

    if (dialog.exec() != QDialog::Accepted || list->currentItem() == nullptr)
        return std::nullopt;
    const auto install = openGameInstall(list->currentItem()->data(rootRole).toString().toStdString());
    return install ? std::optional(*install) : std::nullopt;
}

Result<QByteArray> downloadMagic(QWidget *parent)
{
    QNetworkAccessManager network;
    QNetworkRequest request { QUrl(magicUrl) };
    request.setTransferTimeout(downloadTimeoutMs);
    QNetworkReply *reply = network.get(request);
    QProgressDialog progress(QObject::tr("Downloading the key tables (magic.dat) from the CodeWalker repository…"), QObject::tr("Cancel"), 0, 0, parent);
    progress.setWindowTitle(QStringLiteral("ARCHON"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received) {
        if (received > magicSizeLimit)
            reply->abort();
    });
    progress.show();
    loop.exec();
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return std::unexpected(Error { "download", 0, reply->errorString().toStdString() });
    QByteArray content = reply->readAll();
    if (const auto checked = checkMagic(bytesOf(content)); !checked)
        return std::unexpected(checked.error());
    return content;
}

Result<QByteArray> readLocalMagic(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly) || file.size() > magicSizeLimit)
        return std::unexpected(Error { "magic", 0, "cannot read " + fileName.toStdString() });
    QByteArray content = file.readAll();
    if (const auto checked = checkMagic(bytesOf(content)); !checked)
        return std::unexpected(checked.error());
    return content;
}

bool obtainMagic(QWidget *parent, const QString &target)
{
    auto content = downloadMagic(parent);
    while (!content) {
        const auto answer = QMessageBox::question(parent, QStringLiteral("ARCHON"),
            QObject::tr("The key tables (magic.dat) could not be obtained:\n%1\n\nChoose a local copy of magic.dat instead?").arg(QString::fromStdString(content.error().message)));
        if (answer != QMessageBox::Yes)
            return false;
        const QString fileName = QFileDialog::getOpenFileName(parent, QObject::tr("magic.dat"), QString(), QStringLiteral("magic.dat (*.dat);;All files (*)"));
        if (fileName.isEmpty())
            return false;
        content = readLocalMagic(fileName);
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(*content) != content->size() || !file.commit()) {
        QMessageBox::critical(parent, QStringLiteral("ARCHON"), QObject::tr("Cannot write %1.").arg(target));
        return false;
    }
    return true;
}

}

Result<void> checkMagic(std::span<const std::byte> bytes)
{
    const auto digest = sha256(bytes);
    const QByteArray hex = QByteArray(reinterpret_cast<const char *>(digest.data()), static_cast<qsizetype>(digest.size())).toHex();
    if (bytes.empty() || hex != magicDigest)
        return std::unexpected(Error { "magic", 0, "magic.dat does not match the pinned SHA-256" });
    return {};
}

std::optional<GameInstall> chooseInstall(QWidget *parent)
{
    QSettings stored = settings();
    const QString saved = stored.value(pathSetting).toString();
    if (!saved.isEmpty()) {
        if (const auto install = openGameInstall(saved.toStdString()))
            return *install;
    }
    const std::vector<GameInstall> found = findSteamInstalls(QDir::homePath().toStdString());
    const std::optional<GameInstall> chosen = found.size() == 1 ? std::optional(found.front()) : askForInstall(parent, found);
    if (chosen)
        stored.setValue(pathSetting, QString::fromStdString(chosen->root.string()));
    return chosen;
}

std::optional<GameSession> openSession(QWidget *parent)
{
    const auto install = chooseInstall(parent);
    if (!install)
        return std::nullopt;
    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString magic = dataDirectory + QStringLiteral("/magic.dat");
    const std::filesystem::path cache = (dataDirectory + QStringLiteral("/keys.bin")).toStdString();

    auto keys = std::make_unique<Result<Keys>>(loadKeys(install->executable, magic.toStdString(), cache));
    if (!*keys && !QFile::exists(magic)) {
        if (!obtainMagic(parent, magic))
            return std::nullopt;
        *keys = loadKeys(install->executable, magic.toStdString(), cache);
    }
    if (!*keys) {
        QMessageBox::critical(parent, QStringLiteral("ARCHON"), QObject::tr("The archive keys could not be derived from %1:\n%2")
            .arg(QString::fromStdString(install->executable.string()), QString::fromStdString(keys->error().message)));
        settings().remove(pathSetting);
        return std::nullopt;
    }
    return GameSession { *install, std::make_shared<const Keys>(std::move(**keys)) };
}

}
