#include "app/style.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#include <QSvgRenderer>

namespace archon {

namespace {

const char *const styleSheet = R"(
QMainWindow, QDialog { background: #ffffff; }
QMenuBar { background: #ffffff; border: none; }
QMenuBar::item { padding: 3px 7px; background: transparent; }
QMenuBar::item:selected { background: #e5f3ff; }
QMenu { background: #f2f2f2; border: 1px solid #a0a0a0; }
QMenu::item { padding: 4px 28px 4px 24px; }
QMenu::item:selected { background: #91c9f7; color: #000000; }
QMenu::item:disabled { color: #6d6d6d; }
QMenu::separator { height: 1px; background: #d7d7d7; margin: 3px 0; }
QToolBar { background: #ffffff; border: none; spacing: 2px; padding: 1px 4px; }
QToolBar QToolButton { border: 1px solid transparent; padding: 2px; }
QToolBar QToolButton:hover { background: #e5f3ff; border-color: #cce8ff; }
QToolBar QToolButton:pressed { background: #cce8ff; border-color: #99d1ff; }
QTreeView { background: #ffffff; border: 1px solid #d9d9d9; outline: none; }
QTreeView::item { padding: 1px 0; border: 1px solid transparent; }
QTreeView::item:hover { background: #e5f3ff; }
QTreeView::item:selected { background: #cce8ff; border-color: #99d1ff; color: #000000; }
QHeaderView::section { background: #ffffff; border: none; border-right: 1px solid #e5e5e5; padding: 4px 6px; }
QStatusBar { background: #ffffff; border-top: 1px solid #d9d9d9; }
QStatusBar::item { border: none; }
QStatusBar QLabel { border: 1px solid #c8c8c8; padding: 1px 6px; }
QLineEdit { border: 1px solid #c8c8c8; padding: 3px 6px; background: #ffffff; selection-background-color: #3399ff; selection-color: #ffffff; }
QLineEdit:focus { border-color: #3399ff; }
QSplitter::handle { background: #ffffff; }
QPushButton { border: 1px solid #adadad; background: #e1e1e1; padding: 4px 16px; min-width: 60px; }
QPushButton:hover { border-color: #0078d7; background: #e5f1fb; }
QPushButton:pressed { background: #cce4f7; }
QPushButton:disabled { color: #838383; background: #ececec; border-color: #c8c8c8; }
#navigationRow { background: #ffffff; }
#navigationRow QToolButton { border: none; background: transparent; }
#addressBar { border: 1px solid #c8c8c8; background: #ffffff; }
#addressBar QToolButton { border: 1px solid transparent; padding: 3px 5px; }
#addressBar QToolButton:hover { background: #e5f3ff; border-color: #cce8ff; }
#addressBar QLineEdit { border: none; }
#addressBar QLabel { color: #555555; }
)";

}

void applyStyle(QApplication &application)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::AlternateBase, QColor(0xf7, 0xf7, 0xf7));
    palette.setColor(QPalette::Button, QColor(0xe1, 0xe1, 0xe1));
    palette.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::Highlight, QColor(0xcc, 0xe8, 0xff));
    palette.setColor(QPalette::PlaceholderText, QColor(0x6d, 0x6d, 0x6d));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x83, 0x83, 0x83));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x83, 0x83, 0x83));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x83, 0x83, 0x83));
    for (const QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText, QPalette::HighlightedText }) {
        palette.setColor(QPalette::Active, role, QColor(0, 0, 0));
        palette.setColor(QPalette::Inactive, role, QColor(0, 0, 0));
    }
    QApplication::setPalette(palette);
    application.setStyleSheet(QLatin1StringView(styleSheet));
}

QIcon loadIcon(const QString &name)
{
    QSvgRenderer renderer(QStringLiteral(":/icons/%1.svg").arg(name));
    QIcon icon;
    for (const int size : { 16, 24, 32, 48, 64 }) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

}
