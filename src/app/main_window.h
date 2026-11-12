#pragma once

#include "core/game/game_edit.h"
#include "core/game/game_install.h"

#include <QMainWindow>

class QAction;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace archon {

class AddressBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(std::filesystem::path root, std::shared_ptr<const Keys> keys, std::filesystem::path store, GameEdition edition, QWidget *parent = nullptr);

    QString currentPath() const;
    bool navigate(const QString &path);
    bool reveal(const QString &path);
    void extractSelection(const QString &outputDirectory);
    bool editMode() const;
    void setEditMode(bool on);
    void importFiles(const QStringList &files);
    void replaceSelection(const QString &file);
    void deleteSelection();
    void makeFolder(const QString &name);
    void renameSelection(const QString &name);
    void rebuildCurrentArchive();

private:
    bool open(const QString &path, bool record);
    void buildActions();
    void buildNavigationRow();
    void buildViews();
    void buildStatusBar();
    void fillList();
    void fillTreeItem(QTreeWidgetItem *item);
    void resetTree();
    void syncTree();
    void updateState();
    void activate(QTreeWidgetItem *item);
    void chooseExtractFolder();
    void openSearch();
    void showAbout();
    void showListMenu(const QPoint &position);
    QString childPath(const QString &name) const;
    QStringList selectedNames() const;
    bool selectionHasFolder() const;
    bool prepareArchiveEdit();
    bool runEdit(const QString &label, const std::function<Result<void>(GameEditor &)> &work);
    void reload();
    void copySelectionToMods();
    void openHistory();
    void openPackage(const QString &file);
    bool eventFilter(QObject *watched, QEvent *event) override;

    std::filesystem::path root_;
    std::shared_ptr<const Keys> keys_;
    std::filesystem::path store_;
    GameEdition edition_;
    GameBrowser browser_;
    bool editMode_ = false;
    bool inArchive_ = false;
    QString current_;
    QStringList back_;
    QStringList forward_;
    std::vector<BrowserNode> nodes_;

    QTreeWidget *tree_ = nullptr;
    QTreeWidget *list_ = nullptr;
    AddressBar *address_ = nullptr;
    QLineEdit *filter_ = nullptr;
    QLabel *shownLabel_ = nullptr;
    QLabel *selectedLabel_ = nullptr;
    QLabel *locationLabel_ = nullptr;
    QAction *backAction_ = nullptr;
    QAction *forwardAction_ = nullptr;
    QAction *upAction_ = nullptr;
    QAction *refreshAction_ = nullptr;
    QAction *extractAction_ = nullptr;
    QAction *openAction_ = nullptr;
    QAction *selectAllAction_ = nullptr;
    QAction *searchAction_ = nullptr;
    QAction *exitAction_ = nullptr;
    QAction *aboutAction_ = nullptr;
    QAction *editModeAction_ = nullptr;
    QAction *addAction_ = nullptr;
    QAction *newFolderAction_ = nullptr;
    QAction *replaceAction_ = nullptr;
    QAction *renameAction_ = nullptr;
    QAction *deleteAction_ = nullptr;
    QAction *copyToModsAction_ = nullptr;
    QAction *rebuildAction_ = nullptr;
    QAction *historyAction_ = nullptr;
    QAction *asiManagerAction_ = nullptr;
    QAction *packageInstallerAction_ = nullptr;
};

}
