#pragma once

#include "core/crypto/keys.h"

#include <QDialog>

#include <filesystem>
#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace archon {

class SearchDialog : public QDialog {
    Q_OBJECT
public:
    SearchDialog(std::filesystem::path root, std::shared_ptr<const Keys> keys, QWidget *parent = nullptr);

signals:
    void entryChosen(const QString &path);

private:
    void search();

    std::filesystem::path root_;
    std::shared_ptr<const Keys> keys_;
    QLineEdit *pattern_;
    QPushButton *button_;
    QTreeWidget *results_;
    QLabel *summary_;
};

}
