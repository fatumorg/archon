#pragma once

#include <QIcon>

class QApplication;

namespace archon {

void applyStyle(QApplication &application);
QIcon loadIcon(const QString &name);

}
