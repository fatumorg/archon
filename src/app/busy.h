#pragma once

#include <QString>

#include <atomic>
#include <functional>

class QWidget;

namespace archon {

bool runBusy(QWidget *parent, const QString &label, const std::function<void(const std::atomic<bool> &cancel)> &work);
void runBusy(QWidget *parent, const QString &label, const std::function<void()> &work);

}
