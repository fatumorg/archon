#pragma once

#include "core/prefix/prefix.h"

#include <QDialog>

#include <filesystem>

class QLabel;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QTreeWidget;

namespace archon {

class PrefixSetupDialog : public QDialog {
    Q_OBJECT
public:
    PrefixSetupDialog(std::filesystem::path prefix, GameEdition edition, std::filesystem::path record, std::filesystem::path proc, QWidget *parent = nullptr);

    void start();

    void reject() override;

signals:
    void setupFinished(bool complete);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void showStatus();
    void runNext();
    void finish(bool complete, const QString &message);
    void log(const QString &line);

    std::filesystem::path prefix_;
    GameEdition edition_;
    std::filesystem::path record_;
    std::filesystem::path proc_;
    std::vector<QStringList> pending_;
    QString program_;
    QTreeWidget *components_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QPushButton *start_ = nullptr;
    QPushButton *close_ = nullptr;
    QProcess *process_ = nullptr;
    QStringList recordLines_;
    QString processOutput_;
};

}
