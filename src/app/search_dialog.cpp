#include "app/search_dialog.h"

#include "app/busy.h"
#include "app/display_format.h"
#include "app/style.h"
#include "core/search/entry_search.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace archon {

namespace {

constexpr std::size_t resultLimit = 20000;

}

SearchDialog::SearchDialog(std::filesystem::path root, std::shared_ptr<const Keys> keys, QWidget *parent)
    : QDialog(parent)
    , root_(std::move(root))
    , keys_(std::move(keys))
    , pattern_(new QLineEdit(this))
    , button_(new QPushButton(tr("Search"), this))
    , results_(new QTreeWidget(this))
    , summary_(new QLabel(tr("Wildcards * and ? are supported. All archives are searched."), this))
{
    setWindowTitle(tr("Search"));
    resize(820, 520);
    pattern_->setObjectName(QStringLiteral("searchPattern"));
    pattern_->setPlaceholderText(tr("File name"));
    button_->setObjectName(QStringLiteral("searchButton"));
    button_->setEnabled(false);
    button_->setDefault(true);
    results_->setObjectName(QStringLiteral("searchResults"));
    results_->setHeaderLabels({ tr("Name"), tr("Location"), tr("Type"), tr("Size") });
    results_->setRootIsDecorated(false);
    results_->setUniformRowHeights(true);
    results_->header()->resizeSection(0, 220);
    results_->header()->resizeSection(1, 340);
    results_->header()->resizeSection(2, 120);

    auto *top = new QHBoxLayout;
    top->addWidget(pattern_, 1);
    top->addWidget(button_);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(results_, 1);
    layout->addWidget(summary_);

    connect(pattern_, &QLineEdit::textChanged, this, [this](const QString &text) { button_->setEnabled(!text.trimmed().isEmpty()); });
    connect(button_, &QPushButton::clicked, this, &SearchDialog::search);
    connect(results_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) { emit entryChosen(item->data(0, Qt::UserRole).toString()); });
}

void SearchDialog::search()
{
    const std::string pattern = pattern_->text().trimmed().toStdString();
    if (pattern.empty())
        return;
    SearchResult found;
    const bool finished = runBusy(this, tr("Searching for %1…").arg(pattern_->text().trimmed()), [&](const std::atomic<bool> &cancel) {
        GameBrowser browser(root_, keys_);
        found = searchEntries(browser, "", pattern, cancel, resultLimit);
    });

    results_->clear();
    const QIcon folder = loadIcon(QStringLiteral("folder"));
    const QIcon archive = loadIcon(QStringLiteral("archive"));
    const QIcon file = loadIcon(QStringLiteral("file"));
    QList<QTreeWidgetItem *> items;
    items.reserve(static_cast<qsizetype>(found.hits.size()));
    for (const SearchHit &hit : found.hits) {
        const QString path = QString::fromStdString(hit.path);
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        auto *item = new QTreeWidgetItem({ QString::fromStdString(hit.node.name), slash < 0 ? QString() : path.left(slash), typeName(hit.node), sizeText(hit.node) });
        item->setIcon(0, hit.node.kind == NodeKind::Folder ? folder : hit.node.kind == NodeKind::Archive ? archive : file);
        item->setData(0, Qt::UserRole, path);
        item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        items.append(item);
    }
    results_->addTopLevelItems(items);

    QString text = tr("%1 found").arg(found.hits.size());
    if (found.truncated)
        text += tr(", stopped at the first %1").arg(resultLimit);
    if (!finished)
        text += tr(", cancelled");
    if (found.skipped != 0)
        text += tr(", %1 locations could not be read").arg(found.skipped);
    summary_->setText(text);
}

}
