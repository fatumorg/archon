#include "app/address_bar.h"
#include "app/main_window.h"
#include "app/search_dialog.h"
#include "support/browse_fixture.h"
#include "support/temp_files.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

#include <fstream>

using namespace archon;
using namespace archon::test;

namespace {

void closeNextMessageBox(int *closed = nullptr)
{
    QTimer::singleShot(50, qApp, [closed] {
        if (QWidget *box = QApplication::activeModalWidget()) {
            if (closed != nullptr)
                ++*closed;
            box->close();
        }
    });
}

QTreeWidgetItem *row(QTreeWidget *list, const QString &name)
{
    const auto found = list->findItems(name, Qt::MatchExactly, 0);
    return found.isEmpty() ? nullptr : found.first();
}

QStringList statusTexts(const MainWindow &window)
{
    QStringList texts;
    for (const QLabel *label : window.statusBar()->findChildren<QLabel *>())
        texts.append(label->text());
    return texts;
}

}

class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void showsRootOnStart();
    void navigatesByActivationAndHistory();
    void fillsColumns();
    void filterNarrowsList();
    void failedNavigationKeepsLocation();
    void wrongCaseAddressResolves();
    void typedAddressNavigates();
    void breadcrumbNavigates();
    void treeFollowsLocation();
    void treeSelectionNavigates();
    void selectionUpdatesStatusAndExtract();
    void extractsSelection();
    void revealSelectsEntry();
    void actionsAreExactlyTheWorkingSet();
    void searchDialogFindsEntries();

private:
    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::unique_ptr<MainWindow> window_;
    QTreeWidget *list_ = nullptr;
    QTreeWidget *tree_ = nullptr;
};

void TestMainWindow::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    writeBrowseFixture(root_);
    window_ = std::make_unique<MainWindow>(root_, nullptr);
    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
    list_ = window_->findChild<QTreeWidget *>(QStringLiteral("fileList"));
    tree_ = window_->findChild<QTreeWidget *>(QStringLiteral("navigationTree"));
    QVERIFY(list_ != nullptr && tree_ != nullptr);
}

void TestMainWindow::cleanup()
{
    window_.reset();
}

void TestMainWindow::showsRootOnStart()
{
    QCOMPARE(window_->windowTitle(), QStringLiteral("ARCHON"));
    QCOMPARE(window_->currentPath(), QString());
    QCOMPARE(list_->topLevelItemCount(), 3);
    QCOMPARE(list_->topLevelItem(0)->text(0), QStringLiteral("update"));
    QCOMPARE(list_->topLevelItem(1)->text(0), QStringLiteral("common.rpf"));
    QCOMPARE(list_->topLevelItem(2)->text(0), QStringLiteral("GTA5.exe"));
    const QStringList status = statusTexts(*window_);
    QVERIFY(status.contains(QStringLiteral("3 items shown")));
    QVERIFY(status.contains(QStringLiteral("No selected items")));
    QVERIFY(status.contains(QStringLiteral("Folder")));
    QVERIFY(status.contains(QStringLiteral("Grand Theft Auto V")));
    QVERIFY(status.contains(QStringLiteral("Windows")));
    QStringList headers;
    for (int column = 0; column < list_->columnCount(); ++column)
        headers.append(list_->headerItem()->text(column));
    QCOMPARE(headers, QStringList({ "Name", "Type", "Size", "Attributes", "Tags" }));
}

void TestMainWindow::navigatesByActivationAndHistory()
{
    auto *back = window_->findChild<QToolButton *>(QStringLiteral("backButton"));
    auto *forward = window_->findChild<QToolButton *>(QStringLiteral("forwardButton"));
    QVERIFY(back != nullptr && forward != nullptr);
    QVERIFY(!back->isEnabled());
    QVERIFY(!forward->isEnabled());

    emit list_->itemActivated(row(list_, QStringLiteral("common.rpf")), 0);
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf"));
    QVERIFY(statusTexts(*window_).contains(QStringLiteral("RPF Archive")));
    emit list_->itemActivated(row(list_, QStringLiteral("data")), 0);
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf/data"));

    emit list_->itemActivated(row(list_, QStringLiteral("a.txt")), 0);
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf/data"));

    back->click();
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf"));
    QVERIFY(forward->isEnabled());
    forward->click();
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf/data"));
    QVERIFY(!forward->isEnabled());

    QTest::keyClick(window_.get(), Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf"));
    QTest::keyClick(window_.get(), Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(window_->currentPath(), QString());
    QTest::keyClick(window_.get(), Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(window_->currentPath(), QString());
}

void TestMainWindow::fillsColumns()
{
    QVERIFY(window_->navigate(QStringLiteral("common.rpf/data")));
    const QTreeWidgetItem *packed = row(list_, QStringLiteral("b.bin"));
    QVERIFY(packed != nullptr);
    QCOMPARE(packed->text(1), QStringLiteral("BIN file"));
    QCOMPARE(packed->text(2), QStringLiteral("4.883 Kb"));
    QCOMPARE(packed->text(3), QStringLiteral("Compressed;"));
    QCOMPARE(packed->text(4), QStringLiteral("No"));
    QVERIFY(!packed->icon(0).isNull());

    QVERIFY(window_->navigate(QStringLiteral("common.rpf")));
    QCOMPARE(row(list_, QStringLiteral("data"))->text(2), QStringLiteral("2 items"));
    QCOMPARE(row(list_, QStringLiteral("data"))->text(3), QStringLiteral("Folder"));
    QCOMPARE(row(list_, QStringLiteral("inner.rpf"))->text(1), QStringLiteral("RPF Archive"));
    QCOMPARE(row(list_, QStringLiteral("x.ytd"))->text(3), QStringLiteral("Resource;"));
}

void TestMainWindow::filterNarrowsList()
{
    QVERIFY(window_->navigate(QStringLiteral("common.rpf")));
    auto *filter = window_->findChild<QLineEdit *>(QStringLiteral("filterBox"));
    QVERIFY(filter != nullptr);
    QCOMPARE(filter->placeholderText(), QStringLiteral("Search"));
    filter->setText(QStringLiteral("*.ytd"));
    QCOMPARE(list_->topLevelItemCount(), 1);
    QVERIFY(statusTexts(*window_).contains(QStringLiteral("1 items shown")));
    filter->clear();
    QCOMPARE(list_->topLevelItemCount(), 3);
    filter->setText(QStringLiteral("inn"));
    QCOMPARE(list_->topLevelItemCount(), 1);
    QVERIFY(window_->navigate(QStringLiteral("common.rpf/data")));
    QVERIFY(filter->text().isEmpty());
    QCOMPARE(list_->topLevelItemCount(), 2);
}

void TestMainWindow::failedNavigationKeepsLocation()
{
    writeFile(root_ / "bad.rpf", "0123456789");
    QVERIFY(window_->navigate(QStringLiteral("common.rpf")));
    int closed = 0;
    closeNextMessageBox(&closed);
    QVERIFY(!window_->navigate(QStringLiteral("bad.rpf")));
    QCOMPARE(closed, 1);
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf"));
    QCOMPARE(list_->topLevelItemCount(), 3);
    closeNextMessageBox(&closed);
    QVERIFY(!window_->navigate(QStringLiteral("no/such/place")));
    QCOMPARE(closed, 2);
    closeNextMessageBox(&closed);
    QVERIFY(!window_->navigate(QStringLiteral("../..")));
    QCOMPARE(closed, 3);
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf"));
}

void TestMainWindow::wrongCaseAddressResolves()
{
    QVERIFY(window_->navigate(QStringLiteral("COMMON.RPF/DATA")));
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf/data"));
}

void TestMainWindow::typedAddressNavigates()
{
    auto *address = window_->findChild<AddressBar *>(QStringLiteral("addressBar"));
    QVERIFY(address != nullptr);
    address->beginEdit();
    auto *edit = address->findChild<QLineEdit *>();
    QVERIFY(edit != nullptr && edit->isVisible());
    edit->setText(QStringLiteral("common.rpf\\inner.rpf"));
    QTest::keyClick(edit, Qt::Key_Return);
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf/inner.rpf"));
    QVERIFY(!edit->isVisible());

    address->beginEdit();
    QCOMPARE(edit->text(), QStringLiteral("common.rpf/inner.rpf"));
    edit->setText(QStringLiteral("update"));
    QTest::keyClick(edit, Qt::Key_Escape);
    QVERIFY(!edit->isVisible());
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf/inner.rpf"));
}

void TestMainWindow::breadcrumbNavigates()
{
    QVERIFY(window_->navigate(QStringLiteral("common.rpf/inner.rpf/deep")));
    auto *address = window_->findChild<AddressBar *>(QStringLiteral("addressBar"));
    QStringList crumbs;
    for (const QToolButton *button : address->findChildren<QToolButton *>())
        crumbs.append(button->text());
    QCOMPARE(crumbs, QStringList({ "ARCHON", "GTA V", "common.rpf", "inner.rpf", "deep" }));
    address->findChildren<QToolButton *>().at(2)->click();
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf"));
    address->findChildren<QToolButton *>().at(1)->click();
    QCOMPARE(window_->currentPath(), QString());
}

void TestMainWindow::treeFollowsLocation()
{
    QCOMPARE(tree_->topLevelItemCount(), 1);
    QCOMPARE(tree_->topLevelItem(0)->text(0), QStringLiteral("GTA V"));
    QCOMPARE(tree_->currentItem(), tree_->topLevelItem(0));
    QStringList rootChildren;
    for (int i = 0; i < tree_->topLevelItem(0)->childCount(); ++i)
        rootChildren.append(tree_->topLevelItem(0)->child(i)->text(0));
    QCOMPARE(rootChildren, QStringList({ "update", "common.rpf" }));

    QVERIFY(window_->navigate(QStringLiteral("common.rpf/data")));
    QCOMPARE(tree_->currentItem()->text(0), QStringLiteral("data"));
    QCOMPARE(tree_->currentItem()->parent()->text(0), QStringLiteral("common.rpf"));
    QVERIFY(tree_->currentItem()->parent()->isExpanded());
}

void TestMainWindow::treeSelectionNavigates()
{
    QTreeWidgetItem *root = tree_->topLevelItem(0);
    QTreeWidgetItem *update = root->child(0);
    update->setExpanded(true);
    QCOMPARE(update->childCount(), 1);
    QCOMPARE(update->child(0)->text(0), QStringLiteral("update.rpf"));
    tree_->setCurrentItem(update->child(0));
    QCOMPARE(window_->currentPath(), QStringLiteral("update/update.rpf"));
    QCOMPARE(list_->topLevelItemCount(), 1);
}

void TestMainWindow::selectionUpdatesStatusAndExtract()
{
    QVERIFY(window_->navigate(QStringLiteral("common.rpf/data")));
    QAction *extract = nullptr;
    for (QAction *action : window_->findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("Extract…"))
            extract = action;
    }
    QVERIFY(extract != nullptr);
    QVERIFY(!extract->isEnabled());
    list_->selectAll();
    QVERIFY(extract->isEnabled());
    QVERIFY(statusTexts(*window_).contains(QStringLiteral("Selected: 2")));
    list_->clearSelection();
    QVERIFY(!extract->isEnabled());
    QVERIFY(statusTexts(*window_).contains(QStringLiteral("No selected items")));
}

void TestMainWindow::extractsSelection()
{
    const std::filesystem::path output = pathOf(*directory_) / "out";
    QVERIFY(window_->navigate(QStringLiteral("common.rpf")));
    row(list_, QStringLiteral("data"))->setSelected(true);
    row(list_, QStringLiteral("x.ytd"))->setSelected(true);
    int closed = 0;
    closeNextMessageBox(&closed);
    window_->extractSelection(QString::fromStdString(output.string()));
    QTRY_COMPARE(closed, 1);
    std::ifstream stream(output / "data" / "a.txt");
    QCOMPARE(std::string(std::istreambuf_iterator<char>(stream), {}), std::string("hello"));
    QVERIFY(std::filesystem::exists(output / "data" / "b.bin"));
    QVERIFY(std::filesystem::exists(output / "x.ytd"));
}

void TestMainWindow::revealSelectsEntry()
{
    QVERIFY(window_->reveal(QStringLiteral("common.rpf/inner.rpf/deep/c.txt")));
    QCOMPARE(window_->currentPath(), QStringLiteral("common.rpf/inner.rpf/deep"));
    QCOMPARE(list_->selectedItems().size(), 1);
    QCOMPARE(list_->selectedItems().first()->text(0), QStringLiteral("c.txt"));
}

void TestMainWindow::actionsAreExactlyTheWorkingSet()
{
    const QAction *toolBarToggle = window_->findChild<QToolBar *>()->toggleViewAction();
    QStringList texts;
    for (const QAction *action : window_->findChildren<QAction *>()) {
        if (action != toolBarToggle && !action->isSeparator() && action->menu() == nullptr && !action->text().isEmpty())
            texts.append(action->text());
    }
    texts.removeDuplicates();
    texts.sort();
    QStringList expected { "E&xit", "Extract…", "Open", "Select &all", "&Refresh", "&Up one level", "&Back", "&Forward", "&Search…", "&About ARCHON" };
    expected.sort();
    QCOMPARE(texts, expected);
}

void TestMainWindow::searchDialogFindsEntries()
{
    SearchDialog dialog(root_, nullptr);
    dialog.show();
    auto *pattern = dialog.findChild<QLineEdit *>(QStringLiteral("searchPattern"));
    auto *button = dialog.findChild<QPushButton *>(QStringLiteral("searchButton"));
    auto *results = dialog.findChild<QTreeWidget *>(QStringLiteral("searchResults"));
    QVERIFY(pattern != nullptr && button != nullptr && results != nullptr);
    QVERIFY(!button->isEnabled());
    pattern->setText(QStringLiteral("c.txt"));
    QVERIFY(button->isEnabled());
    button->click();
    QCOMPARE(results->topLevelItemCount(), 1);
    QCOMPARE(results->topLevelItem(0)->text(0), QStringLiteral("c.txt"));
    QCOMPARE(results->topLevelItem(0)->text(1), QStringLiteral("common.rpf/inner.rpf/deep"));

    QSignalSpy chosen(&dialog, &SearchDialog::entryChosen);
    emit results->itemActivated(results->topLevelItem(0), 0);
    QCOMPARE(chosen.size(), 1);
    QCOMPARE(chosen.first().first().toString(), QStringLiteral("common.rpf/inner.rpf/deep/c.txt"));

    pattern->setText(QStringLiteral("nothing-matches"));
    button->click();
    QCOMPARE(results->topLevelItemCount(), 0);
}

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
