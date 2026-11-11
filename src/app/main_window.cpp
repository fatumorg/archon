#include "app/main_window.h"

#include "app/address_bar.h"
#include "app/busy.h"
#include "app/display_format.h"
#include "app/search_dialog.h"
#include "app/style.h"
#include "core/io/ascii.h"
#include "core/search/entry_search.h"
#include "core/version.h"

#include <QApplication>
#include <QCollator>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace archon {

namespace {

constexpr int nodeRole = Qt::UserRole;
constexpr int pathRole = Qt::UserRole;
constexpr int filledRole = Qt::UserRole + 1;
constexpr int sizeColumn = 2;

const QCollator &nameOrder()
{
    static const QCollator collator = [] {
        QCollator made;
        made.setCaseSensitivity(Qt::CaseInsensitive);
        made.setNumericMode(true);
        return made;
    }();
    return collator;
}

bool nodeBefore(const BrowserNode &left, const BrowserNode &right)
{
    if (left.kind != right.kind)
        return left.kind < right.kind;
    return nameOrder().compare(QString::fromStdString(left.name), QString::fromStdString(right.name)) < 0;
}

QIcon iconOf(NodeKind kind)
{
    static const QIcon folder = loadIcon(QStringLiteral("folder"));
    static const QIcon archive = loadIcon(QStringLiteral("archive"));
    static const QIcon file = loadIcon(QStringLiteral("file"));
    return kind == NodeKind::Folder ? folder : kind == NodeKind::Archive ? archive : file;
}

class ListItem : public QTreeWidgetItem {
public:
    ListItem(const BrowserNode &node, int index)
        : QTreeWidgetItem({ QString::fromStdString(node.name), typeName(node), sizeText(node), attributesText(node), QStringLiteral("No") })
        , node_(node)
    {
        setIcon(0, iconOf(node.kind));
        setData(0, nodeRole, index);
        setTextAlignment(sizeColumn, Qt::AlignRight | Qt::AlignVCenter);
    }

    bool operator<(const QTreeWidgetItem &other) const override
    {
        const BrowserNode &left = node_;
        const BrowserNode &right = static_cast<const ListItem &>(other).node_;
        const int column = treeWidget() != nullptr ? treeWidget()->sortColumn() : 0;
        if (column == 0)
            return nodeBefore(left, right);
        if (column == sizeColumn && left.kind == right.kind && left.size != right.size)
            return left.size < right.size;
        const int order = nameOrder().compare(text(column), other.text(column));
        return order != 0 ? order < 0 : nodeBefore(left, right);
    }

private:
    BrowserNode node_;
};

QString parentPath(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

bool isInsideArchive(const QString &path)
{
    for (const QString &component : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (component.endsWith(QLatin1StringView(".rpf"), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

}

MainWindow::MainWindow(std::filesystem::path root, std::shared_ptr<const Keys> keys, QWidget *parent)
    : QMainWindow(parent)
    , root_(std::move(root))
    , keys_(std::move(keys))
    , browser_(root_, keys_)
{
    setWindowTitle(QStringLiteral("ARCHON"));
    setWindowIcon(loadIcon(QStringLiteral("archon")));
    resize(1000, 720);
    buildActions();
    buildNavigationRow();
    buildViews();
    buildStatusBar();
    resetTree();
    open(QString(), false);
}

QString MainWindow::currentPath() const
{
    return current_;
}

bool MainWindow::navigate(const QString &path)
{
    return open(path, true);
}

bool MainWindow::reveal(const QString &path)
{
    const auto canonical = browser_.canonicalPath(path.toStdString());
    if (!canonical) {
        QMessageBox::warning(this, QStringLiteral("ARCHON"), QString::fromStdString(canonical.error().message));
        return false;
    }
    const QString found = QString::fromStdString(*canonical);
    if (!open(parentPath(found), true))
        return false;
    const QString name = found.mid(found.lastIndexOf(QLatin1Char('/')) + 1);
    for (QTreeWidgetItem *item : list_->findItems(name, Qt::MatchExactly, 0)) {
        list_->setCurrentItem(item);
        list_->scrollToItem(item);
        break;
    }
    return true;
}

bool MainWindow::open(const QString &path, bool record)
{
    const auto canonical = browser_.canonicalPath(path.toStdString());
    auto nodes = canonical ? browser_.list(*canonical) : Result<std::vector<BrowserNode>>(std::unexpected(canonical.error()));
    if (!nodes) {
        syncTree();
        QMessageBox::warning(this, QStringLiteral("ARCHON"), QString::fromStdString(nodes.error().message));
        return false;
    }
    const QString target = QString::fromStdString(*canonical);
    if (record && target != current_) {
        back_.append(current_);
        forward_.clear();
    }
    current_ = target;
    nodes_ = std::move(*nodes);
    std::ranges::sort(nodes_, nodeBefore);
    {
        const QSignalBlocker blocker(filter_);
        filter_->clear();
    }
    address_->setPath(current_);
    fillList();
    syncTree();
    return true;
}

void MainWindow::buildActions()
{
    backAction_ = new QAction(loadIcon(QStringLiteral("back")), tr("&Back"), this);
    backAction_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    connect(backAction_, &QAction::triggered, this, [this] {
        if (back_.isEmpty())
            return;
        const QString target = back_.takeLast();
        const QString origin = current_;
        if (open(target, false))
            forward_.append(origin);
        else
            back_.append(target);
        updateState();
    });

    forwardAction_ = new QAction(loadIcon(QStringLiteral("forward")), tr("&Forward"), this);
    forwardAction_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    connect(forwardAction_, &QAction::triggered, this, [this] {
        if (forward_.isEmpty())
            return;
        const QString target = forward_.takeLast();
        const QString origin = current_;
        if (open(target, false))
            back_.append(origin);
        else
            forward_.append(target);
        updateState();
    });

    upAction_ = new QAction(loadIcon(QStringLiteral("up")), tr("&Up one level"), this);
    upAction_->setShortcuts({ QKeySequence(Qt::ALT | Qt::Key_Up), QKeySequence(Qt::Key_Backspace) });
    connect(upAction_, &QAction::triggered, this, [this] {
        if (!current_.isEmpty())
            navigate(parentPath(current_));
    });

    refreshAction_ = new QAction(loadIcon(QStringLiteral("refresh")), tr("&Refresh"), this);
    refreshAction_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refreshAction_, &QAction::triggered, this, [this] {
        browser_ = GameBrowser(root_, keys_);
        resetTree();
        if (!open(current_, false))
            open(QString(), false);
    });

    extractAction_ = new QAction(loadIcon(QStringLiteral("extract")), tr("Extract…"), this);
    extractAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(extractAction_, &QAction::triggered, this, &MainWindow::chooseExtractFolder);

    openAction_ = new QAction(tr("Open"), this);
    connect(openAction_, &QAction::triggered, this, [this] { activate(list_->currentItem()); });

    selectAllAction_ = new QAction(tr("Select &all"), this);
    selectAllAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
    connect(selectAllAction_, &QAction::triggered, this, [this] { list_->selectAll(); });

    searchAction_ = new QAction(loadIcon(QStringLiteral("search")), tr("&Search…"), this);
    searchAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F3));
    connect(searchAction_, &QAction::triggered, this, &MainWindow::openSearch);

    exitAction_ = new QAction(tr("E&xit"), this);
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);

    aboutAction_ = new QAction(tr("&About ARCHON"), this);
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildNavigationRow()
{
    auto *top = new QWidget(this);
    auto *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);

    auto *row = new QWidget(top);
    row->setObjectName(QStringLiteral("navigationRow"));
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(8, 6, 8, 4);
    rowLayout->setSpacing(6);

    auto *back = new QToolButton(row);
    back->setObjectName(QStringLiteral("backButton"));
    back->setDefaultAction(backAction_);
    back->setIconSize(QSize(40, 40));
    back->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto *forward = new QToolButton(row);
    forward->setObjectName(QStringLiteral("forwardButton"));
    forward->setDefaultAction(forwardAction_);
    forward->setIconSize(QSize(28, 28));
    forward->setToolButtonStyle(Qt::ToolButtonIconOnly);

    address_ = new AddressBar(row);
    address_->setObjectName(QStringLiteral("addressBar"));
    connect(address_, &AddressBar::pathRequested, this, &MainWindow::navigate);

    filter_ = new QLineEdit(row);
    filter_->setObjectName(QStringLiteral("filterBox"));
    filter_->setPlaceholderText(tr("Search"));
    filter_->setClearButtonEnabled(true);
    filter_->setFixedWidth(250);
    filter_->setFixedHeight(30);
    connect(filter_, &QLineEdit::textChanged, this, &MainWindow::fillList);

    rowLayout->addWidget(back);
    rowLayout->addWidget(forward);
    rowLayout->addWidget(address_, 1);
    rowLayout->addWidget(filter_);

    auto *menus = new QMenuBar(top);
    QMenu *file = menus->addMenu(tr("&File"));
    file->addAction(exitAction_);
    QMenu *edit = menus->addMenu(tr("&Edit"));
    edit->addAction(extractAction_);
    edit->addSeparator();
    edit->addAction(selectAllAction_);
    QMenu *view = menus->addMenu(tr("&View"));
    view->addAction(backAction_);
    view->addAction(forwardAction_);
    view->addAction(upAction_);
    view->addSeparator();
    view->addAction(refreshAction_);
    QMenu *tools = menus->addMenu(tr("&Tools"));
    tools->addAction(searchAction_);
    QMenu *help = menus->addMenu(tr("&Help"));
    help->addAction(aboutAction_);

    topLayout->addWidget(row);
    topLayout->addWidget(menus);
    setMenuWidget(top);

    QToolBar *bar = addToolBar(tr("Actions"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setContextMenuPolicy(Qt::PreventContextMenu);
    bar->setIconSize(QSize(20, 20));
    bar->addAction(upAction_);
    bar->addAction(refreshAction_);
    bar->addSeparator();
    bar->addAction(extractAction_);
    bar->addAction(searchAction_);
    setContextMenuPolicy(Qt::NoContextMenu);
}

void MainWindow::buildViews()
{
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("navigationTree"));
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setIconSize(QSize(18, 18));
    connect(tree_, &QTreeWidget::itemExpanded, this, &MainWindow::fillTreeItem);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (item != nullptr && item->data(0, pathRole).toString() != current_)
            navigate(item->data(0, pathRole).toString());
    });

    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("fileList"));
    list_->setHeaderLabels({ tr("Name"), tr("Type"), tr("Size"), tr("Attributes"), tr("Tags") });
    list_->headerItem()->setTextAlignment(sizeColumn, Qt::AlignRight | Qt::AlignVCenter);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setAllColumnsShowFocus(true);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setIconSize(QSize(18, 18));
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionsMovable(false);
    list_->header()->resizeSection(0, 220);
    list_->header()->resizeSection(1, 135);
    list_->header()->resizeSection(2, 80);
    list_->header()->resizeSection(3, 145);
    list_->header()->resizeSection(4, 110);
    list_->setSortingEnabled(true);
    list_->sortByColumn(0, Qt::AscendingOrder);
    connect(list_, &QTreeWidget::itemActivated, this, &MainWindow::activate);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateState);
    connect(list_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showListMenu);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(tree_);
    splitter->addWidget(list_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 225, 775 });
    splitter->setChildrenCollapsible(false);
    splitter->setContentsMargins(8, 0, 8, 2);
    setCentralWidget(splitter);
}

void MainWindow::buildStatusBar()
{
    shownLabel_ = new QLabel(this);
    selectedLabel_ = new QLabel(this);
    locationLabel_ = new QLabel(this);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(shownLabel_);
    statusBar()->addWidget(selectedLabel_);
    statusBar()->addPermanentWidget(locationLabel_);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("Grand Theft Auto V"), this));
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("Windows"), this));
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("ARCHON %1").arg(versionString()), this));
}

void MainWindow::fillList()
{
    const std::string pattern = filter_->text().trimmed().toStdString();
    QList<QTreeWidgetItem *> items;
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        if (pattern.empty() || matchesPattern(pattern, nodes_[index].name))
            items.append(new ListItem(nodes_[index], static_cast<int>(index)));
    }
    list_->clear();
    list_->addTopLevelItems(items);
    updateState();
}

void MainWindow::fillTreeItem(QTreeWidgetItem *item)
{
    if (item->data(0, filledRole).toBool())
        return;
    item->setData(0, filledRole, true);
    qDeleteAll(item->takeChildren());
    const QString path = item->data(0, pathRole).toString();
    auto nodes = browser_.list(path.toStdString());
    if (nodes) {
        std::ranges::sort(*nodes, nodeBefore);
        for (const BrowserNode &node : *nodes) {
            if (node.kind == NodeKind::File)
                continue;
            const QString name = QString::fromStdString(node.name);
            auto *child = new QTreeWidgetItem(item, { name });
            child->setIcon(0, iconOf(node.kind));
            child->setData(0, pathRole, path.isEmpty() ? name : path + QLatin1Char('/') + name);
            child->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
    }
    if (item->childCount() == 0)
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
}

void MainWindow::resetTree()
{
    const QSignalBlocker blocker(tree_);
    tree_->clear();
    auto *root = new QTreeWidgetItem(tree_, { QStringLiteral("GTA V") });
    root->setIcon(0, loadIcon(QStringLiteral("archon")));
    root->setData(0, pathRole, QString());
    fillTreeItem(root);
    root->setExpanded(true);
}

void MainWindow::syncTree()
{
    const QSignalBlocker blocker(tree_);
    QTreeWidgetItem *item = tree_->topLevelItem(0);
    for (const QString &component : current_.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        fillTreeItem(item);
        QTreeWidgetItem *next = nullptr;
        for (int i = 0; i < item->childCount() && next == nullptr; ++i) {
            if (item->child(i)->text(0) == component)
                next = item->child(i);
        }
        if (next == nullptr)
            break;
        item->setExpanded(true);
        item = next;
    }
    tree_->setCurrentItem(item);
    tree_->scrollToItem(item);
}

void MainWindow::updateState()
{
    const qsizetype selected = list_->selectedItems().size();
    shownLabel_->setText(tr("%1 items shown").arg(list_->topLevelItemCount()));
    selectedLabel_->setText(selected == 0 ? tr("No selected items") : tr("Selected: %1").arg(selected));
    locationLabel_->setText(isInsideArchive(current_) ? tr("RPF Archive") : tr("Folder"));
    backAction_->setEnabled(!back_.isEmpty());
    forwardAction_->setEnabled(!forward_.isEmpty());
    upAction_->setEnabled(!current_.isEmpty());
    extractAction_->setEnabled(selected != 0);
}

void MainWindow::activate(QTreeWidgetItem *item)
{
    if (item == nullptr)
        return;
    const BrowserNode &node = nodes_[static_cast<std::size_t>(item->data(0, nodeRole).toInt())];
    if (node.kind != NodeKind::File)
        navigate(childPath(QString::fromStdString(node.name)));
}

QString MainWindow::childPath(const QString &name) const
{
    return current_.isEmpty() ? name : current_ + QLatin1Char('/') + name;
}

void MainWindow::chooseExtractFolder()
{
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Extract to"));
    if (!directory.isEmpty())
        extractSelection(directory);
}

void MainWindow::extractSelection(const QString &outputDirectory)
{
    std::vector<std::string> paths;
    for (const QTreeWidgetItem *item : list_->selectedItems())
        paths.push_back(childPath(item->text(0)).toStdString());
    if (paths.empty())
        return;

    ExtractSummary total {};
    const std::filesystem::path output(outputDirectory.toStdString());
    const bool finished = runBusy(this, tr("Extracting…"), [&](const std::atomic<bool> &cancel) {
        GameBrowser browser(root_, keys_);
        for (const std::string &path : paths) {
            if (cancel.load())
                return;
            auto summary = browser.extract(path, output);
            if (!summary) {
                Error error = summary.error();
                error.message = path + ": " + error.message;
                total.failures.push_back(std::move(error));
                continue;
            }
            total.files += summary->files;
            total.bytes += summary->bytes;
            std::ranges::move(summary->failures, std::back_inserter(total.failures));
        }
    });

    QString text = tr("Extracted %1 files (%2) to %3.").arg(total.files).arg(formatSize(total.bytes), outputDirectory);
    if (!finished)
        text += QLatin1Char('\n') + tr("Extraction was cancelled before it finished.");
    if (total.failures.empty()) {
        QMessageBox::information(this, QStringLiteral("ARCHON"), text);
        return;
    }
    text += QLatin1Char('\n') + tr("%1 entries could not be extracted:").arg(total.failures.size());
    constexpr std::size_t shownFailures = 10;
    for (std::size_t i = 0; i < std::min(total.failures.size(), shownFailures); ++i)
        text += QLatin1Char('\n') + QString::fromStdString(total.failures[i].message);
    QMessageBox::warning(this, QStringLiteral("ARCHON"), text);
}

void MainWindow::openSearch()
{
    SearchDialog dialog(root_, keys_, this);
    connect(&dialog, &SearchDialog::entryChosen, this, [this, &dialog](const QString &path) {
        if (reveal(path))
            dialog.accept();
    });
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About ARCHON"),
        tr("<b>ARCHON %1</b><p>Archive browser and mod tool for Grand Theft Auto V.</p><p>ARCHON ships no game files and no keys. Keys are derived from your own copy of the game.</p>").arg(versionString()));
}

void MainWindow::showListMenu(const QPoint &position)
{
    QTreeWidgetItem *item = list_->itemAt(position);
    if (item == nullptr)
        return;
    QMenu menu(this);
    if (nodes_[static_cast<std::size_t>(item->data(0, nodeRole).toInt())].kind != NodeKind::File)
        menu.addAction(openAction_);
    menu.addAction(extractAction_);
    menu.exec(list_->viewport()->mapToGlobal(position));
}

}
