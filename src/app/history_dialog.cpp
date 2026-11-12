#include "app/history_dialog.h"

#include "app/busy.h"
#include "core/safety/transaction.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace archon {

namespace {

Result<void> rollBackBusy(QWidget *parent, const std::filesystem::path &store, const std::string &id)
{
    Result<void> result;
    runBusy(parent, QObject::tr("Rolling back…"), [&] { result = rollbackTransaction(store, id); });
    return result;
}

QString filesText(const std::vector<std::filesystem::path> &files)
{
    QStringList names;
    for (const std::filesystem::path &file : files)
        names.append(QString::fromStdString(file.filename().string()));
    return names.join(QStringLiteral(", "));
}

}

HistoryDialog::HistoryDialog(std::filesystem::path store, QWidget *parent)
    : QDialog(parent)
    , store_(std::move(store))
{
    setWindowTitle(tr("Change history"));
    resize(720, 420);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("historyList"));
    list_->setHeaderLabels({ tr("Time"), tr("Change"), tr("Files") });
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setAllColumnsShowFocus(true);
    list_->header()->resizeSection(0, 150);
    list_->header()->resizeSection(1, 330);

    auto *buttons = new QDialogButtonBox(this);
    rollBack_ = buttons->addButton(tr("Roll back"), QDialogButtonBox::ActionRole);
    rollBack_->setObjectName(QStringLiteral("rollBackButton"));
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(rollBack_, &QPushButton::clicked, this, &HistoryDialog::rollBackSelected);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, [this] { rollBack_->setEnabled(list_->currentItem() != nullptr); });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(list_);
    layout->addWidget(buttons);
    fill();
}

void HistoryDialog::fill()
{
    list_->clear();
    for (const TransactionRecord &record : listTransactions(store_)) {
        QString description = QString::fromStdString(record.description);
        if (!record.committed)
            description += tr(" (unfinished)");
        auto *item = new QTreeWidgetItem(list_, { QDateTime::fromMSecsSinceEpoch(record.time).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), description, filesText(record.files) });
        item->setData(0, Qt::UserRole, QString::fromStdString(record.id));
    }
    if (list_->topLevelItemCount() > 0)
        list_->setCurrentItem(list_->topLevelItem(0));
    rollBack_->setEnabled(list_->currentItem() != nullptr);
}

void HistoryDialog::rollBackSelected()
{
    QTreeWidgetItem *item = list_->currentItem();
    if (item == nullptr)
        return;
    const int newer = list_->indexOfTopLevelItem(item);
    QString question = tr("Roll back \"%1\"?").arg(item->text(1));
    if (newer > 0)
        question += QLatin1Char('\n') + (newer == 1 ? tr("1 newer change will be rolled back with it.") : tr("%1 newer changes will be rolled back with it.").arg(newer));
    if (QMessageBox::question(this, QStringLiteral("ARCHON"), question) != QMessageBox::Yes)
        return;
    const auto result = rollBackBusy(this, store_, item->data(0, Qt::UserRole).toString().toStdString());
    fill();
    emit rolledBack();
    if (!result)
        QMessageBox::warning(this, QStringLiteral("ARCHON"), QString::fromStdString(result.error().message));
}

bool recoverUnfinished(const std::filesystem::path &store, QWidget *parent)
{
    for (const TransactionRecord &record : listTransactions(store)) {
        if (record.committed)
            continue;
        QMessageBox box(QMessageBox::Warning, QStringLiteral("ARCHON"),
            QObject::tr("ARCHON stopped while this change was being made:\n%1\n\nRoll it back to restore the files?").arg(QString::fromStdString(record.description)),
            QMessageBox::NoButton, parent);
        QPushButton *rollBack = box.addButton(QObject::tr("Roll back"), QMessageBox::AcceptRole);
        box.addButton(QObject::tr("Quit"), QMessageBox::RejectRole);
        box.setDefaultButton(rollBack);
        box.exec();
        if (box.clickedButton() != rollBack)
            return false;
        if (const auto result = rollBackBusy(parent, store, record.id); !result) {
            QMessageBox::critical(parent, QStringLiteral("ARCHON"), QString::fromStdString(result.error().message));
            return false;
        }
    }
    return true;
}

}
