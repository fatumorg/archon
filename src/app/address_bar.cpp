#include "app/address_bar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStackedLayout>
#include <QToolButton>

namespace archon {

AddressBar::AddressBar(QWidget *parent)
    : QFrame(parent)
    , pages_(new QStackedLayout(this))
    , crumbs_(new QWidget(this))
    , crumbLayout_(new QHBoxLayout(crumbs_))
    , edit_(new QLineEdit(this))
{
    crumbLayout_->setContentsMargins(4, 0, 4, 0);
    crumbLayout_->setSpacing(0);
    pages_->setContentsMargins(0, 0, 0, 0);
    pages_->addWidget(crumbs_);
    pages_->addWidget(edit_);
    edit_->installEventFilter(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(30);
    connect(edit_, &QLineEdit::returnPressed, this, [this] {
        const QString requested = edit_->text().trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
        endEdit();
        emit pathRequested(requested);
    });
    setPath(QString());
}

void AddressBar::setPath(const QString &path)
{
    path_ = path;
    while (QLayoutItem *item = crumbLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    QStringList names { QStringLiteral("ARCHON"), QStringLiteral("GTA V") };
    QStringList targets { QString(), QString() };
    const QStringList components = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < components.size(); ++i) {
        names.append(components.at(i));
        targets.append(components.mid(0, i + 1).join(QLatin1Char('/')));
    }
    for (qsizetype i = 0; i < names.size(); ++i) {
        auto *button = new QToolButton(crumbs_);
        button->setText(names.at(i));
        button->setFocusPolicy(Qt::NoFocus);
        const QString target = targets.at(i);
        connect(button, &QToolButton::clicked, this, [this, target] { emit pathRequested(target); });
        crumbLayout_->addWidget(button);
        auto *separator = new QLabel(QStringLiteral("▸"), crumbs_);
        separator->setAttribute(Qt::WA_TransparentForMouseEvents);
        crumbLayout_->addWidget(separator);
    }
    crumbLayout_->addStretch(1);
}

void AddressBar::beginEdit()
{
    edit_->setText(path_);
    pages_->setCurrentWidget(edit_);
    edit_->setFocus();
    edit_->selectAll();
}

void AddressBar::endEdit()
{
    pages_->setCurrentWidget(crumbs_);
}

void AddressBar::mousePressEvent(QMouseEvent *event)
{
    QFrame::mousePressEvent(event);
    beginEdit();
}

bool AddressBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == edit_) {
        if (event->type() == QEvent::FocusOut)
            endEdit();
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            endEdit();
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

}
