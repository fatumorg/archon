#pragma once

#include "core/io/error.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

class QWidget;

namespace archon {

Result<QByteArray> download(QWidget *parent, const QString &label, const QUrl &url, qint64 limit);

}
