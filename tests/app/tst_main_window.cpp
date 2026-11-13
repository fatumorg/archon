#include "app/address_bar.h"
#include "app/busy.h"
#include "app/history_dialog.h"
#include "app/text_editor.h"
#include "app/texture_viewer.h"
#include "app/main_window.h"
#include "app/model_viewer.h"
#include "app/search_dialog.h"
#include "app/style.h"
#include "core/game/game_browser.h"
#include "core/meta/metadata_document.h"
#include "core/rsc/resource_file.h"
#include "core/rsc/rsc7.h"
#include "core/ytd/texture_pixels.h"
#include "support/rpf_builder.h"
#include "support/test_bytes.h"
#include "core/rpf/rpf_reader.h"
#include "core/safety/transaction.h"
#include "support/browse_fixture.h"
#include "support/drawable_samples.h"
#include "support/meta_samples.h"
#include "support/modal_driver.h"
#include "support/zip_builder.h"
#include "support/temp_files.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QFile>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

#include <deque>
#include <fstream>
#include <thread>

using namespace archon;
using namespace archon::test;

namespace {

void closeNextMessageBox(int *closed = nullptr)
{
    auto *timer = new QTimer(qApp);
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, closed] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box == nullptr)
            return;
        if (closed != nullptr)
            ++*closed;
        timer->stop();
        timer->deleteLater();
        box->close();
    });
    timer->start();
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

QAction *actionNamed(QWidget *window, const QString &text)
{
    for (QAction *action : window->findChildren<QAction *>()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

std::string entryText(const std::filesystem::path &archive, std::string_view path)
{
    const auto reader = RpfReader::open(openFileSource(archive), archive.filename().string(), nullptr);
    if (!reader)
        return "<" + reader.error().message + ">";
    const auto index = reader->archive().find(path);
    if (!index)
        return "<missing>";
    const auto data = reader->fileData(*index);
    return data ? std::string(reinterpret_cast<const char *>(data->data()), data->size()) : "<unreadable>";
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
    void editActionsDisabledUntilEditMode();
    void editModeNeedsConfirmation();
    void importIntoModsArchive();
    void editOutsideModsOffersCopy();
    void deleteRenameNewFolderReplace();
    void rebuildKeepsEntries();
    void historyRollbackRestores();
    void recoverUnfinishedRollsBack();
    void styledMessageBoxButtonsFitTheirText();
    void uncancellableWorkShowsNoCancelButton();
    void asiManagerOpensFromTools();
    void activatingOivOpensPackageInstaller();
    void texturesOpenEditAndSave();
    void editionIsShownAndGameChoiceCanBeCancelled();
    void textMetadataOpensEditsAndSaves();
    void hugeTextFileIsRefusedBeforeReading();
    void binaryMetadataOpensAsXmlAndSaves();
    void drawableOpensModelViewer();

private:
    void turnOnEditMode()
    {
        ModalDriver driver({ QStringLiteral("Yes") });
        window_->setEditMode(true);
        QVERIFY(window_->editMode());
    }
    void copyToMods()
    {
        std::filesystem::create_directories(root_ / "mods");
        std::filesystem::copy_file(root_ / "common.rpf", root_ / "mods" / "common.rpf");
    }
    std::filesystem::path source(const std::string &name, std::string_view content)
    {
        const std::filesystem::path file = pathOf(*directory_) / "sources" / name;
        writeFile(file, content);
        return file;
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::filesystem::path store_;
    std::unique_ptr<MainWindow> window_;
    QTreeWidget *list_ = nullptr;
    QTreeWidget *tree_ = nullptr;
};

void TestMainWindow::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    store_ = pathOf(*directory_) / "store";
    writeBrowseFixture(root_);
    window_ = std::make_unique<MainWindow>(root_, nullptr, store_, GameEdition::Legacy);
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

    emit list_->itemActivated(row(list_, QStringLiteral("b.bin")), 0);
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
    QStringList expected { "E&xit", "Extract…", "Open", "Select &all", "&Refresh", "&Up one level", "&Back", "&Forward", "&Search…", "&About ARCHON",
        "Edit mode", "&Add…", "&New folder…", "&Replace…", "Re&name…", "&Delete", "Copy to \"mods\" folder", "Re&build", "Change &history…", "&ASI Manager…", "&Package Installer…", "Package &Creator…", "&Choose game…" };
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

void TestMainWindow::editActionsDisabledUntilEditMode()
{
    copyToMods();
    QVERIFY(window_->navigate(QStringLiteral("mods/common.rpf/data")));
    list_->selectAll();
    const QStringList editing { "&Add…", "&New folder…", "&Delete", "Re&build" };
    for (const QString &text : editing)
        QVERIFY2(!actionNamed(window_.get(), text)->isEnabled(), qPrintable(text));
    turnOnEditMode();
    list_->clearSelection();
    row(list_, QStringLiteral("a.txt"))->setSelected(true);
    for (const QString &text : editing + QStringList { "&Replace…", "Re&name…" })
        QVERIFY2(actionNamed(window_.get(), text)->isEnabled(), qPrintable(text));
    QVERIFY(!actionNamed(window_.get(), QStringLiteral("Copy to \"mods\" folder"))->isEnabled());

    QVERIFY(window_->navigate(QString()));
    row(list_, QStringLiteral("update"))->setSelected(true);
    QVERIFY(!actionNamed(window_.get(), QStringLiteral("Re&build"))->isEnabled());
    QVERIFY(!actionNamed(window_.get(), QStringLiteral("Re&name…"))->isEnabled());
    QVERIFY(!actionNamed(window_.get(), QStringLiteral("&Delete"))->isEnabled());
    QVERIFY(actionNamed(window_.get(), QStringLiteral("&Add…"))->isEnabled());
}

void TestMainWindow::editModeNeedsConfirmation()
{
    QAction *edit = actionNamed(window_.get(), QStringLiteral("Edit mode"));
    QVERIFY(edit != nullptr && edit->isCheckable());
    QCOMPARE(edit->shortcut(), QKeySequence(Qt::Key_F6));
    QVERIFY(!window_->editMode());
    {
        bool checkedWhileAsking = false;
        QTimer probe;
        probe.setInterval(5);
        connect(&probe, &QTimer::timeout, this, [&] {
            if (qobject_cast<QMessageBox *>(QApplication::activeModalWidget()) != nullptr)
                checkedWhileAsking = checkedWhileAsking || edit->isChecked();
        });
        probe.start();
        ModalDriver driver({ QStringLiteral("No") });
        edit->trigger();
        QVERIFY(driver.done());
        QVERIFY(!checkedWhileAsking);
    }
    QVERIFY(!window_->editMode());
    QVERIFY(!edit->isChecked());
    {
        ModalDriver driver({ QStringLiteral("Yes") });
        edit->trigger();
        QVERIFY(driver.done());
    }
    QVERIFY(window_->editMode());
    QVERIFY(edit->isChecked());
    edit->trigger();
    QVERIFY(!window_->editMode());
}

void TestMainWindow::importIntoModsArchive()
{
    copyToMods();
    turnOnEditMode();
    QVERIFY(window_->navigate(QStringLiteral("mods/common.rpf/data")));
    {
        ModalDriver driver({});
        window_->importFiles({ QString::fromStdString(source("added.txt", "new file").string()) });
        QVERIFY2(driver.unexpected.isEmpty(), qPrintable(driver.unexpected.join(QLatin1Char('\n'))));
    }
    QVERIFY(row(list_, QStringLiteral("added.txt")) != nullptr);
    QCOMPARE(entryText(root_ / "mods" / "common.rpf", "data/added.txt"), std::string("new file"));

    ModalDriver overwrite({ QStringLiteral("Yes") });
    window_->importFiles({ QString::fromStdString(source("A.TXT", "overwritten").string()) });
    QVERIFY(overwrite.done());
    QCOMPARE(overwrite.seen, QStringList { QStringLiteral("1 item with the same name already exists here and will be replaced. Continue?") });
    QCOMPARE(entryText(root_ / "mods" / "common.rpf", "data/a.txt"), std::string("overwritten"));
}

void TestMainWindow::editOutsideModsOffersCopy()
{
    const auto original = readFile(root_ / "common.rpf");
    turnOnEditMode();
    QVERIFY(window_->navigate(QStringLiteral("common.rpf/data")));
    ModalDriver driver({ QStringLiteral("Copy to \"mods\" folder") });
    window_->importFiles({ QString::fromStdString(source("added.txt", "new file").string()) });
    QVERIFY(driver.done());
    QVERIFY2(driver.unexpected.isEmpty(), qPrintable(driver.unexpected.join(QLatin1Char('\n'))));
    QCOMPARE(window_->currentPath(), QStringLiteral("mods/common.rpf/data"));
    QVERIFY(row(list_, QStringLiteral("added.txt")) != nullptr);
    QCOMPARE(readFile(root_ / "common.rpf"), original);
    QCOMPARE(entryText(root_ / "mods" / "common.rpf", "data/added.txt"), std::string("new file"));
}

void TestMainWindow::deleteRenameNewFolderReplace()
{
    copyToMods();
    turnOnEditMode();
    QVERIFY(window_->navigate(QStringLiteral("mods/common.rpf/data")));
    {
        ModalDriver driver({});
        window_->makeFolder(QStringLiteral("made"));
        QVERIFY(row(list_, QStringLiteral("made")) != nullptr);

        row(list_, QStringLiteral("a.txt"))->setSelected(true);
        window_->renameSelection(QStringLiteral("renamed.txt"));
        QVERIFY(row(list_, QStringLiteral("a.txt")) == nullptr);
        QVERIFY(row(list_, QStringLiteral("renamed.txt")) != nullptr);

        list_->clearSelection();
        row(list_, QStringLiteral("renamed.txt"))->setSelected(true);
        window_->replaceSelection(QString::fromStdString(source("replacement.txt", "replaced").string()));
        QCOMPARE(entryText(root_ / "mods" / "common.rpf", "data/renamed.txt"), std::string("replaced"));
        QVERIFY2(driver.unexpected.isEmpty(), qPrintable(driver.unexpected.join(QLatin1Char('\n'))));
    }
    list_->clearSelection();
    row(list_, QStringLiteral("b.bin"))->setSelected(true);
    {
        ModalDriver driver({ QStringLiteral("No") });
        window_->deleteSelection();
        QVERIFY(driver.done());
    }
    QVERIFY(row(list_, QStringLiteral("b.bin")) != nullptr);
    row(list_, QStringLiteral("b.bin"))->setSelected(true);
    {
        ModalDriver driver({ QStringLiteral("Yes") });
        window_->deleteSelection();
        QVERIFY(driver.done());
    }
    QVERIFY(row(list_, QStringLiteral("b.bin")) == nullptr);
    QCOMPARE(listTransactions(store_).size(), std::size_t(4));
}

void TestMainWindow::rebuildKeepsEntries()
{
    copyToMods();
    turnOnEditMode();
    QVERIFY(window_->navigate(QStringLiteral("mods/common.rpf")));
    {
        ModalDriver driver({});
        window_->importFiles({ QString::fromStdString(source("big.txt", std::string(300000, 'k')).string()) });
        QVERIFY2(driver.unexpected.isEmpty(), qPrintable(driver.unexpected.join(QLatin1Char('\n'))));
    }
    row(list_, QStringLiteral("big.txt"))->setSelected(true);
    {
        ModalDriver confirm({ QStringLiteral("Yes") });
        window_->deleteSelection();
        QVERIFY(confirm.done());
    }
    const auto edited = std::filesystem::file_size(root_ / "mods" / "common.rpf");
    ModalDriver driver({});
    window_->rebuildCurrentArchive();
    QVERIFY2(driver.unexpected.isEmpty(), qPrintable(driver.unexpected.join(QLatin1Char('\n'))));
    QVERIFY(std::filesystem::file_size(root_ / "mods" / "common.rpf") < edited);
    QCOMPARE(window_->currentPath(), QStringLiteral("mods/common.rpf"));
    QCOMPARE(list_->topLevelItemCount(), 3);
    QCOMPARE(entryText(root_ / "mods" / "common.rpf", "data/a.txt"), std::string("hello"));
}

void TestMainWindow::historyRollbackRestores()
{
    copyToMods();
    turnOnEditMode();
    QVERIFY(window_->navigate(QStringLiteral("mods/common.rpf/data")));
    {
        ModalDriver driver({});
        window_->importFiles({ QString::fromStdString(source("added.txt", "new file").string()) });
        window_->makeFolder(QStringLiteral("made"));
    }
    QVERIFY(row(list_, QStringLiteral("added.txt")) != nullptr);

    {
        HistoryDialog dialog(store_);
        auto *history = dialog.findChild<QTreeWidget *>(QStringLiteral("historyList"));
        QVERIFY(history != nullptr);
        QCOMPARE(history->topLevelItemCount(), 2);
        QCOMPARE(history->topLevelItem(1)->text(1), QStringLiteral("Add 1 file to mods/common.rpf/data"));
        history->setCurrentItem(history->topLevelItem(1));
        ModalDriver driver({ QStringLiteral("No") });
        auto *rollBack = dialog.findChild<QPushButton *>(QStringLiteral("rollBackButton"));
        QVERIFY(rollBack != nullptr);
        rollBack->click();
        QCOMPARE(driver.seen, QStringList { QStringLiteral("Roll back \"Add 1 file to mods/common.rpf/data\"?\n1 newer change will be rolled back with it.") });
    }

    ModalDriver driver({ QStringLiteral("Roll back"), QStringLiteral("Yes"), QStringLiteral("Close") });
    actionNamed(window_.get(), QStringLiteral("Change &history…"))->trigger();
    QVERIFY(driver.done());
    QCOMPARE(driver.seen, QStringList { QStringLiteral("Roll back \"New folder mods/common.rpf/data/made\"?") });
    QVERIFY2(driver.unexpected.isEmpty(), qPrintable(driver.unexpected.join(QLatin1Char('\n'))));
    QVERIFY(row(list_, QStringLiteral("made")) == nullptr);
    QVERIFY(row(list_, QStringLiteral("added.txt")) != nullptr);
    QCOMPARE(listTransactions(store_).size(), std::size_t(1));
}

void TestMainWindow::recoverUnfinishedRollsBack()
{
    window_.reset();
    const auto original = readFile(root_ / "GTA5.exe");
    {
        auto transaction = Transaction::begin(store_, root_, "Replace GTA5.exe");
        QVERIFY(transaction);
        QVERIFY(transaction->writeAt(root_ / "GTA5.exe", 0, std::as_bytes(std::span("XXXX", 4))));
        [[maybe_unused]] auto *abandoned = new Transaction(std::move(*transaction));
    }
    QVERIFY(readFile(root_ / "GTA5.exe") != original);
    {
        ModalDriver driver({ QStringLiteral("Quit") });
        QVERIFY(!recoverUnfinished(store_, nullptr));
        QVERIFY(driver.done());
    }
    QCOMPARE(listTransactions(store_).size(), std::size_t(1));
    {
        ModalDriver driver({ QStringLiteral("Roll back") });
        QVERIFY(recoverUnfinished(store_, nullptr));
        QVERIFY(driver.done());
    }
    QCOMPARE(readFile(root_ / "GTA5.exe"), original);
    QVERIFY(listTransactions(store_).empty());
    QVERIFY(recoverUnfinished(store_, nullptr));
}

void TestMainWindow::styledMessageBoxButtonsFitTheirText()
{
    window_.reset();
    applyStyle(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    QMessageBox box(QMessageBox::Question, QStringLiteral("ARCHON"), QStringLiteral("This archive is in the game folder."));
    const QStringList texts { QStringLiteral("Copy to \"mods\" folder"), QStringLiteral("Edit in game folder") };
    for (const QString &text : texts)
        box.addButton(text, QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.show();
    QVERIFY(QTest::qWaitForWindowExposed(&box));
    for (QAbstractButton *button : box.findChildren<QAbstractButton *>()) {
        if (!button->isVisible())
            continue;
        const int needed = button->fontMetrics().horizontalAdvance(plain(button->text())) + 2 * 16;
        QVERIFY2(button->width() >= needed, qPrintable(QStringLiteral("%1: %2 < %3").arg(button->text()).arg(button->width()).arg(needed)));
    }
}

void TestMainWindow::uncancellableWorkShowsNoCancelButton()
{
    int dialogs = 0;
    int cancelButtons = 0;
    bool dismissedEarly = false;
    QProgressDialog *poked = nullptr;
    std::atomic<bool> ran { false };
    QTimer probe;
    probe.setInterval(10);
    connect(&probe, &QTimer::timeout, this, [&] {
        auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget());
        if (poked != nullptr && !ran.load() && (progress == nullptr || !progress->isVisible()))
            dismissedEarly = true;
        if (progress == nullptr || poked != nullptr)
            return;
        ++dialogs;
        for (const QPushButton *button : progress->findChildren<QPushButton *>())
            cancelButtons += button->isVisible() ? 1 : 0;
        poked = progress;
        QTest::keyClick(progress, Qt::Key_Escape);
        progress->close();
    });
    probe.start();
    runBusy(window_.get(), QStringLiteral("Working…"), [&ran] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        ran = true;
    });
    probe.stop();
    QVERIFY(ran.load());
    QVERIFY(dialogs > 0);
    QCOMPARE(cancelButtons, 0);
    QVERIFY(!dismissedEarly);
}

void TestMainWindow::asiManagerOpensFromTools()
{
    QStringList titles;
    QTimer probe;
    probe.setInterval(20);
    connect(&probe, &QTimer::timeout, this, [&titles] {
        if (QWidget *modal = QApplication::activeModalWidget(); modal != nullptr && !titles.contains(modal->windowTitle()))
            titles.append(modal->windowTitle());
    });
    probe.start();
    ModalDriver driver({ QStringLiteral("Close") });
    actionNamed(window_.get(), QStringLiteral("&ASI Manager…"))->trigger();
    QVERIFY(driver.done());
    QCOMPARE(titles, QStringList { QStringLiteral("ASI Manager") });
    QVERIFY(window_->findChild<QToolBar *>()->actions().contains(actionNamed(window_.get(), QStringLiteral("&ASI Manager…"))));
}

void TestMainWindow::activatingOivOpensPackageInstaller()
{
    const std::string assembly = "<package version=\"2.2\" id=\"{1}\" target=\"Five\"><metadata><name>Listed</name><version><major>1</major><minor>0</minor></version><author><displayName>A</displayName></author><description>d</description></metadata><colors/><content/></package>";
    writeFile(root_ / "mod.oiv", makeZip({ { "assembly.xml", assembly } }));
    writeFile(root_ / "broken.oiv", "not a zip");
    QVERIFY(window_->navigate(QStringLiteral("update")));
    QVERIFY(window_->navigate(QString()));
    QStringList titles;
    QTimer probe;
    probe.setInterval(20);
    connect(&probe, &QTimer::timeout, this, [&titles] {
        if (QWidget *modal = QApplication::activeModalWidget(); modal != nullptr && !titles.contains(modal->windowTitle()))
            titles.append(modal->windowTitle());
    });
    probe.start();
    {
        ModalDriver driver({ QStringLiteral("Close") });
        emit list_->itemActivated(row(list_, QStringLiteral("mod.oiv")), 0);
        QVERIFY(driver.done());
    }
    QCOMPARE(titles, QStringList { QStringLiteral("Package Installer") });
    {
        ModalDriver driver({ QStringLiteral("OK") });
        emit list_->itemActivated(row(list_, QStringLiteral("broken.oiv")), 0);
        QVERIFY(driver.done());
        QCOMPARE(driver.seen.size(), 1);
        QVERIFY(driver.seen.first().startsWith(QStringLiteral("This file is not an OIV package")));
    }
    QVERIFY(window_->findChild<QToolBar *>()->actions().contains(actionNamed(window_.get(), QStringLiteral("&Package Installer…"))));
}

void TestMainWindow::texturesOpenEditAndSave()
{
    const std::vector<Texture> textures { rgbaTexture("paint", 4, 4, std::vector<std::byte>(64, std::byte { 9 })) };
    const auto ytd = writeTextureDictionary(textures).value();
    const auto header = parseRsc7Header(std::span(ytd).first(16)).value();
    RpfBuilder archive;
    archive.addBinary("data/a.txt", toVector("hello"), false);
    archive.addResourceBody("cars.ytd", std::vector<std::byte>(ytd.begin() + 16, ytd.end()), header.systemFlags, header.graphicsFlags);
    writeFile(root_ / "mods" / "cars.rpf", archive.build());
    turnOnEditMode();
    QVERIFY(window_->navigate(QStringLiteral("mods/cars.rpf")));

    QStringList seen;
    QStringList boxes;
    QTimer driver;
    driver.setInterval(20);
    connect(&driver, &QTimer::timeout, this, [&seen, &boxes] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            boxes.append(box->text());
            QTimer::singleShot(0, box, &QWidget::close);
            return;
        }
        auto *viewer = qobject_cast<TextureViewer *>(QApplication::activeModalWidget());
        if (viewer == nullptr || seen.contains(viewer->windowTitle()))
            return;
        seen.append(viewer->windowTitle());
        QTimer::singleShot(0, viewer, [viewer] {
            viewer->renameSelected(QStringLiteral("repainted"));
            viewer->save();
            viewer->accept();
        });
    });
    driver.start();
    emit list_->itemActivated(row(list_, QStringLiteral("cars.ytd")), 0);
    QCOMPARE(seen, QStringList { QStringLiteral("cars.ytd - Texture Dictionary") });
    QVERIFY2(boxes.isEmpty(), qPrintable(boxes.join(QLatin1Char('\n'))));

    const auto reader = RpfReader::open(openFileSource(root_ / "mods" / "cars.rpf"), "cars.rpf", nullptr);
    QVERIFY(reader);
    const auto index = reader->archive().find("cars.ytd");
    QVERIFY(index);
    const RpfEntry &entry = reader->archive().entries()[*index];
    const auto read = readTextureDictionary(*reader->fileData(*index), rsc7SizeFromFlags(entry.systemFlags));
    QVERIFY(read);
    QCOMPARE(read->front().name, std::string("repainted"));
    QCOMPARE(listTransactions(store_).front().description, std::string("Replace mods/cars.rpf/cars.ytd"));
}

void TestMainWindow::editionIsShownAndGameChoiceCanBeCancelled()
{
    QVERIFY(statusTexts(*window_).contains(QStringLiteral("Grand Theft Auto V")));
    MainWindow enhanced(root_, nullptr, store_, GameEdition::Enhanced);
    QVERIFY(statusTexts(enhanced).contains(QStringLiteral("Grand Theft Auto V Enhanced")));
    QCOMPARE(enhanced.findChild<QTreeWidget *>(QStringLiteral("navigationTree"))->topLevelItem(0)->text(0), QStringLiteral("GTA V Enhanced"));
    QStringList crumbs;
    for (const QToolButton *button : enhanced.findChild<AddressBar *>(QStringLiteral("addressBar"))->findChildren<QToolButton *>())
        crumbs.append(button->text());
    QCOMPARE(crumbs, QStringList({ "ARCHON", "GTA V Enhanced" }));
    QSignalSpy requested(window_.get(), &MainWindow::gameChangeRequested);
    ModalDriver driver({ QStringLiteral("Cancel") });
    actionNamed(window_.get(), QStringLiteral("&Choose game…"))->trigger();
    QVERIFY(driver.done());
    QCOMPARE(requested.size(), 0);
    QVERIFY(window_->isVisible());
}

void TestMainWindow::textMetadataOpensEditsAndSaves()
{
    copyToMods();
    turnOnEditMode();
    QVERIFY(window_->navigate(QStringLiteral("mods/common.rpf/data")));
    QStringList seen;
    QStringList boxes;
    QTimer driver;
    driver.setInterval(20);
    connect(&driver, &QTimer::timeout, this, [&seen, &boxes] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            boxes.append(box->text());
            QTimer::singleShot(0, box, &QWidget::close);
            return;
        }
        auto *editor = qobject_cast<TextEditor *>(QApplication::activeModalWidget());
        if (editor == nullptr || !seen.isEmpty())
            return;
        seen.append(editor->windowTitle());
        QTimer::singleShot(0, editor, [editor] {
            editor->findChild<QPlainTextEdit *>(QStringLiteral("textEdit"))->setPlainText(QStringLiteral("edited"));
            editor->save();
            editor->accept();
        });
    });
    driver.start();
    emit list_->itemActivated(row(list_, QStringLiteral("a.txt")), 0);
    QCOMPARE(seen.size(), 1);
    QVERIFY(seen.first().startsWith(QStringLiteral("a.txt - ARCHON")));
    QVERIFY2(boxes.isEmpty(), qPrintable(boxes.join(QLatin1Char('\n'))));
    QCOMPARE(entryText(root_ / "mods" / "common.rpf", "data/a.txt"), std::string("edited"));
}

void TestMainWindow::hugeTextFileIsRefusedBeforeReading()
{
    QFile huge(QString::fromStdString((root_ / "update" / "huge.log").string()));
    QVERIFY(huge.open(QIODevice::WriteOnly));
    QVERIFY(huge.resize((qint64(64) << 20) + 1));
    huge.close();
    QVERIFY(huge.setPermissions(QFileDevice::Permissions()));
    QVERIFY(window_->navigate(QStringLiteral("update")));
    QStringList boxes;
    bool editorOpened = false;
    QTimer driver;
    driver.setInterval(20);
    connect(&driver, &QTimer::timeout, this, [&boxes, &editorOpened] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            boxes.append(box->text());
            QTimer::singleShot(0, box, &QWidget::close);
        } else if (auto *editor = qobject_cast<TextEditor *>(QApplication::activeModalWidget())) {
            editorOpened = true;
            QTimer::singleShot(0, editor, &QDialog::accept);
        }
    });
    driver.start();
    emit list_->itemActivated(row(list_, QStringLiteral("huge.log")), 0);
    QVERIFY(!editorOpened);
    QCOMPARE(boxes, QStringList { QStringLiteral("huge.log is larger than 64 MiB") });
}

void TestMainWindow::binaryMetadataOpensAsXmlAndSaves()
{
    RpfBuilder mods;
    mods.addBinary("data/x.ymt", samplePso(), false);
    writeFile(root_ / "mods" / "common.rpf", mods.build());
    turnOnEditMode();
    QVERIFY(window_->navigate(QStringLiteral("mods/common.rpf/data")));
    QString shown;
    QStringList boxes;
    QTimer driver;
    driver.setInterval(20);
    connect(&driver, &QTimer::timeout, this, [&shown, &boxes] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            boxes.append(box->text());
            QTimer::singleShot(0, box, &QWidget::close);
            return;
        }
        auto *editor = qobject_cast<TextEditor *>(QApplication::activeModalWidget());
        if (editor == nullptr || !shown.isEmpty())
            return;
        auto *text = editor->findChild<QPlainTextEdit *>(QStringLiteral("textEdit"));
        shown = text->toPlainText();
        QTimer::singleShot(0, editor, [editor, text] {
            text->setPlainText(text->toPlainText().replace(QStringLiteral("value=\"1.5\""), QStringLiteral("value=\"2.5\"")));
            editor->save();
            editor->accept();
        });
    });
    driver.start();
    emit list_->itemActivated(row(list_, QStringLiteral("x.ymt")), 0);
    QVERIFY2(boxes.isEmpty(), qPrintable(boxes.join(QLatin1Char('\n'))));
    QVERIFY(shown.contains(QStringLiteral("value=\"1.5\" />")));
    QVERIFY(std::filesystem::exists(nameTableFile(store_)));
    const std::string saved = entryText(root_ / "mods" / "common.rpf", "data/x.ymt");
    const auto reopened = openMetadata(std::as_bytes(std::span(saved.data(), saved.size())), NameTable());
    QVERIFY(reopened);
    QVERIFY(reopened->xml.find("value=\"2.5\"") != std::string::npos);
}

void TestMainWindow::drawableOpensModelViewer()
{
    const auto ydr = drawableFile(sampleDrawable(), 165);
    const auto ydrHeader = parseRsc7Header(std::span(ydr).first(16)).value();
    const auto ytd = writeTextureDictionary(std::vector<Texture> { rgbaTexture("tri_d", 4, 4, std::vector<std::byte>(64, std::byte { 9 })) }).value();
    const auto ytdHeader = parseRsc7Header(std::span(ytd).first(16)).value();
    RpfBuilder archive;
    archive.addResourceBody("tri_hi.ydr", std::vector<std::byte>(ydr.begin() + 16, ydr.end()), ydrHeader.systemFlags, ydrHeader.graphicsFlags);
    archive.addResourceBody("plain.ydr", std::vector<std::byte>(ydr.begin() + 16, ydr.end()), ydrHeader.systemFlags, ydrHeader.graphicsFlags);
    archive.addResourceBody("tri+hi.ytd", std::vector<std::byte>(ytd.begin() + 16, ytd.end()), ytdHeader.systemFlags, ytdHeader.graphicsFlags);
    DrawableSampleOptions fragment;
    fragment.kind = SampleKind::Yft;
    const auto yft = drawableFile(sampleDrawable(fragment), 162);
    const auto yftHeader = parseRsc7Header(std::span(yft).first(16)).value();
    archive.addResourceBody("car.yft", std::vector<std::byte>(yft.begin() + 16, yft.end()), yftHeader.systemFlags, yftHeader.graphicsFlags);
    archive.addResourceBody("vehshare.ytd", std::vector<std::byte>(ytd.begin() + 16, ytd.end()), ytdHeader.systemFlags, ytdHeader.graphicsFlags);
    archive.addResourceBody("broken.ydr", std::vector<std::byte>(ydr.begin() + 16, ydr.end()), ydrHeader.systemFlags, ydrHeader.graphicsFlags);
    archive.addResourceBody("broken.ytd", std::vector<std::byte>(32), ytdHeader.systemFlags, ytdHeader.graphicsFlags);
    writeFile(root_ / "models.rpf", archive.build());
    QVERIFY(window_->navigate(QStringLiteral("models.rpf")));
    QStringList seen;
    QStringList boxes;
    QTimer driver;
    driver.setInterval(20);
    connect(&driver, &QTimer::timeout, this, [&seen, &boxes] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            boxes.append(box->text());
            QTimer::singleShot(0, box, &QWidget::close);
        } else if (auto *viewer = qobject_cast<ModelViewer *>(QApplication::activeModalWidget())) {
            seen.append(viewer->windowTitle() + QLatin1Char('|') + viewer->findChild<QLabel *>(QStringLiteral("modelStatus"))->text());
            QTimer::singleShot(0, viewer, &QDialog::reject);
        }
    });
    driver.start();
    emit list_->itemActivated(row(list_, QStringLiteral("tri_hi.ydr")), 0);
    emit list_->itemActivated(row(list_, QStringLiteral("plain.ydr")), 0);
    emit list_->itemActivated(row(list_, QStringLiteral("car.yft")), 0);
    QVERIFY2(boxes.isEmpty(), qPrintable(boxes.join(QLatin1Char('\n'))));
    emit list_->itemActivated(row(list_, QStringLiteral("broken.ydr")), 0);
    QCOMPARE(boxes.size(), 1);
    QVERIFY2(boxes.front().startsWith(QStringLiteral("broken.ytd: ")), qPrintable(boxes.front()));
    QCOMPARE(seen, (QStringList { QStringLiteral("tri_hi.ydr - Model viewer|1 mesh, 1 triangle"), QStringLiteral("plain.ydr - Model viewer|1 mesh, 1 triangle, 1 texture not found"), QStringLiteral("car.yft - Model viewer|1 mesh, 1 triangle"), QStringLiteral("broken.ydr - Model viewer|1 mesh, 1 triangle, 1 texture not found") }));
}

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
