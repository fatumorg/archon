#include "app/busy.h"

#include <QCloseEvent>
#include <QEventLoop>
#include <QProgressDialog>
#include <QThread>

#include <memory>

namespace archon {

namespace {

class BusyDialog : public QProgressDialog {
public:
    BusyDialog(const QString &label, bool cancellable, QWidget *parent)
        : QProgressDialog(label, QObject::tr("Cancel"), 0, 0, parent)
        , cancellable_(cancellable)
    {
        if (!cancellable_)
            setCancelButton(nullptr);
    }

    void reject() override
    {
        if (cancellable_)
            QProgressDialog::reject();
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (cancellable_)
            QProgressDialog::closeEvent(event);
        else
            event->ignore();
    }

private:
    const bool cancellable_;
};

bool run(QWidget *parent, const QString &label, bool cancellable, const std::function<void(const std::atomic<bool> &cancel)> &work)
{
    std::atomic<bool> cancel { false };
    BusyDialog dialog(label, cancellable, parent);
    dialog.setWindowTitle(QStringLiteral("ARCHON"));
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setAutoClose(false);
    dialog.setAutoReset(false);
    dialog.setMinimumDuration(0);
    QObject::connect(&dialog, &QProgressDialog::canceled, &dialog, [&cancel] { cancel = true; });

    const std::unique_ptr<QThread> thread(QThread::create([&work, &cancel] { work(cancel); }));
    QEventLoop loop;
    QObject::connect(thread.get(), &QThread::finished, &loop, &QEventLoop::quit);
    thread->start();
    dialog.show();
    loop.exec();
    thread->wait();
    return !cancel;
}

}

bool runBusy(QWidget *parent, const QString &label, const std::function<void(const std::atomic<bool> &cancel)> &work)
{
    return run(parent, label, true, work);
}

void runBusy(QWidget *parent, const QString &label, const std::function<void()> &work)
{
    run(parent, label, false, [&work](const std::atomic<bool> &) { work(); });
}

}
