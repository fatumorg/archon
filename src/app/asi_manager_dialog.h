#pragma once

#include "core/asi/asi_manager.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace archon {

class AsiManagerDialog : public QDialog {
    Q_OBJECT
public:
    AsiManagerDialog(std::filesystem::path root, GameEdition edition, std::filesystem::path store, QWidget *parent = nullptr);

    void installFrom(const PinnedDownload &which, const QByteArray &bytes);
    void addPlugins(const QStringList &files);

private:
    void refresh();
    void updateButtons();
    void install(const PinnedDownload &which);
    bool apply(const QString &label, const std::function<Result<void>(AsiManager &)> &work);
    QString selectedPlugin() const;

    std::filesystem::path root_;
    GameEdition edition_;
    std::filesystem::path store_;
    AsiState state_ {};
    QLabel *loaderStatus_ = nullptr;
    QLabel *modsLoaderStatus_ = nullptr;
    QPushButton *loaderButton_ = nullptr;
    QPushButton *modsLoaderButton_ = nullptr;
    QTreeWidget *plugins_ = nullptr;
    QPushButton *toggle_ = nullptr;
    QPushButton *remove_ = nullptr;
};

}
