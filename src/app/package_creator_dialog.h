#pragma once

#include "core/oiv/oiv_writer.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace archon {

class PackageCreatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit PackageCreatorDialog(QWidget *parent = nullptr);

    PackageDraft draft() const;
    void addFiles(const QStringList &files, const QString &target);
    void addDelete(const QString &target);
    void removeSelected();
    bool createPackage(const QString &file);
    bool setIcon(const QString &file);

private:
    QWidget *buildPackageTab();
    QWidget *buildContentTab();
    void addRow(const PackageItem &item);
    void pickColor(QPushButton *button, std::uint32_t &color);
    void showColor(QPushButton *button, std::uint32_t color);

    std::vector<PackageItem> items_;
    std::uint32_t headerColor_ = 0xFF23366A;
    std::uint32_t iconColor_ = 0xFF3B5998;
    std::vector<std::byte> icon_;
    QLineEdit *name_ = nullptr;
    QSpinBox *major_ = nullptr;
    QSpinBox *minor_ = nullptr;
    QLineEdit *tag_ = nullptr;
    QLineEdit *author_ = nullptr;
    QLineEdit *link_ = nullptr;
    QPlainTextEdit *description_ = nullptr;
    QLineEdit *historyTitle_ = nullptr;
    QPlainTextEdit *history_ = nullptr;
    QPlainTextEdit *licence_ = nullptr;
    QCheckBox *blackText_ = nullptr;
    QPushButton *headerButton_ = nullptr;
    QPushButton *iconColorButton_ = nullptr;
    QLabel *iconPreview_ = nullptr;
    QTableWidget *table_ = nullptr;
};

}
