#pragma once

#include "core/game/game_browser.h"

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
    MainWindow(std::filesystem::path root, std::shared_ptr<const Keys> keys, QWidget *parent = nullptr);

    QString currentPath() const;
    bool navigate(const QString &path);
    bool reveal(const QString &path);
    void extractSelection(const QString &outputDirectory);

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

    std::filesystem::path root_;
    std::shared_ptr<const Keys> keys_;
    GameBrowser browser_;
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
};

}
