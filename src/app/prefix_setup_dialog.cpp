#include "app/prefix_setup_dialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace archon {

namespace {

constexpr std::array<PrefixComponent, 5> components {
    PrefixComponent::DotNet48,
    PrefixComponent::VcRun2022,
    PrefixComponent::D3dCompiler47,
    PrefixComponent::Windows10,
    PrefixComponent::LoaderOverride,
};

QString title(PrefixComponent component)
{
    const std::string_view text = componentTitle(component);
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

PrefixSetupDialog::PrefixSetupDialog(std::filesystem::path prefix, GameEdition edition, std::filesystem::path record, std::filesystem::path proc, QWidget *parent)
    : QDialog(parent)
    , prefix_(std::move(prefix))
    , edition_(edition)
    , record_(std::move(record))
    , proc_(std::move(proc))
{
    setWindowTitle(tr("Proton prefix setup"));
    resize(720, 520);

    auto *location = new QLabel(tr("Prefix: %1").arg(QString::fromStdString(prefix_.string())), this);
    location->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *warning = new QLabel(tr("Script mods need these runtime components inside the game's Proton prefix. ARCHON installs the missing ones with protontricks. Changes to the Proton prefix cannot be rolled back."), this);
    warning->setObjectName(QStringLiteral("prefixWarning"));
    warning->setWordWrap(true);

    components_ = new QTreeWidget(this);
    components_->setObjectName(QStringLiteral("componentList"));
    components_->setHeaderLabels({ tr("Component"), tr("State") });
    components_->setRootIsDecorated(false);
    components_->header()->resizeSection(0, 520);
    for (const PrefixComponent component : components)
        new QTreeWidgetItem(components_, { title(component), QString() });
    components_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    components_->setFixedHeight(components_->header()->sizeHint().height() + components_->sizeHintForRow(0) * static_cast<int>(components.size()) + 2 * components_->frameWidth() + 2);

    output_ = new QPlainTextEdit(this);
    output_->setObjectName(QStringLiteral("prefixOutput"));
    output_->setReadOnly(true);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto *buttons = new QDialogButtonBox(this);
    start_ = buttons->addButton(tr("Start"), QDialogButtonBox::ActionRole);
    start_->setObjectName(QStringLiteral("startButton"));
    close_ = buttons->addButton(QDialogButtonBox::Close);
    connect(start_, &QPushButton::clicked, this, &PrefixSetupDialog::start);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(location);
    layout->addWidget(warning);
    layout->addWidget(components_);
    layout->addWidget(output_, 1);
    layout->addWidget(buttons);
    showStatus();
}

void PrefixSetupDialog::showStatus()
{
    const auto status = prefixStatus(prefix_, edition_);
    for (std::size_t index = 0; index < components.size(); ++index)
        components_->topLevelItem(static_cast<int>(index))->setText(1, status.at(components[index]) ? tr("Present") : tr("Missing"));
}

void PrefixSetupDialog::log(const QString &line)
{
    output_->appendPlainText(line);
}

void PrefixSetupDialog::start()
{
    if (process_ != nullptr)
        return;
    recordLines_ = { QDateTime::currentDateTime().toString(Qt::ISODate) + QStringLiteral(" prefix setup for ") + QString::fromStdString(prefix_.string()) };
    if (isGameRunning(proc_)) {
        finish(false, tr("Close the game before setting up its prefix."));
        return;
    }
    program_ = QStandardPaths::findExecutable(QStringLiteral("protontricks"));
    if (program_.isEmpty()) {
        finish(false, tr("protontricks was not found. Install it and try again."));
        return;
    }
    pending_ = setupCommands(edition_, prefixStatus(prefix_, edition_));
    if (pending_.empty()) {
        finish(true, tr("Everything is already set up."));
        return;
    }
    start_->setEnabled(false);
    close_->setEnabled(false);
    std::ranges::reverse(pending_);
    runNext();
}

void PrefixSetupDialog::runNext()
{
    if (pending_.empty()) {
        showStatus();
        const auto status = prefixStatus(prefix_, edition_);
        QStringList missing;
        for (const PrefixComponent component : components) {
            if (!status.at(component))
                missing.append(title(component));
        }
        finish(missing.isEmpty(), missing.isEmpty() ? tr("All components are present.") : tr("Not installed: %1").arg(missing.join(QStringLiteral(", "))));
        return;
    }
    const QStringList arguments = pending_.back();
    pending_.pop_back();
    const QString shown = QStringLiteral("protontricks ") + arguments.join(QLatin1Char(' '));
    log(QStringLiteral("$ ") + shown);
    output_->appendPlainText(QString());
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::readyRead, this, [this] {
        const QString text = QString::fromLocal8Bit(process_->readAll());
        output_->moveCursor(QTextCursor::End);
        output_->insertPlainText(text);
        output_->moveCursor(QTextCursor::End);
    });
    connect(process_, &QProcess::finished, this, [this, shown](int code, QProcess::ExitStatus exitStatus) {
        process_->deleteLater();
        process_ = nullptr;
        const int result = exitStatus == QProcess::NormalExit ? code : -1;
        recordLines_.append(shown + QStringLiteral(" -> exit code %1").arg(result));
        if (result != 0)
            log(tr("protontricks exited with code %1").arg(result));
        showStatus();
        runNext();
    });
    connect(process_, &QProcess::errorOccurred, this, [this, shown](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process_->deleteLater();
        process_ = nullptr;
        recordLines_.append(shown + QStringLiteral(" -> could not start"));
        pending_.clear();
        finish(false, tr("protontricks could not be started."));
    });
    process_->start(program_, arguments);
}

void PrefixSetupDialog::finish(bool complete, const QString &message)
{
    log(message);
    const auto status = prefixStatus(prefix_, edition_);
    for (const PrefixComponent component : components)
        recordLines_.append(title(component) + (status.at(component) ? QStringLiteral(": present") : QStringLiteral(": missing")));
    std::error_code error;
    std::filesystem::create_directories(record_.parent_path(), error);
    QFile record(record_);
    if (record.open(QIODevice::Append | QIODevice::Text))
        record.write((recordLines_.join(QLatin1Char('\n')) + QStringLiteral("\n\n")).toUtf8());
    start_->setEnabled(true);
    close_->setEnabled(true);
    showStatus();
    emit setupFinished(complete);
}

}
