#pragma once

#include <QDialog>

#include <filesystem>

class QPushButton;
class QTreeWidget;

namespace archon {

class HistoryDialog : public QDialog {
    Q_OBJECT
public:
    HistoryDialog(std::filesystem::path store, QWidget *parent = nullptr);

signals:
    void rolledBack();

private:
    void fill();
    void rollBackSelected();

    std::filesystem::path store_;
    QTreeWidget *list_ = nullptr;
    QPushButton *rollBack_ = nullptr;
};

bool recoverUnfinished(const std::filesystem::path &store, QWidget *parent);

}
