#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTimer>

#include <deque>

namespace archon::test {

inline QString plain(QString text)
{
    return text.remove(QLatin1Char('&'));
}

class ModalDriver : public QObject {
public:
    explicit ModalDriver(QStringList buttons)
        : buttons_(buttons.begin(), buttons.end())
    {
        timer_.setInterval(20);
        connect(&timer_, &QTimer::timeout, this, &ModalDriver::poll);
        timer_.start();
    }

    bool done() const
    {
        return buttons_.empty();
    }

    QStringList unexpected;
    QStringList seen;

private:
    void poll()
    {
        QWidget *modal = QApplication::activeModalWidget();
        if (modal == nullptr || qobject_cast<QProgressDialog *>(modal) != nullptr || modal == last_)
            return;
        if (buttons_.empty()) {
            auto *box = qobject_cast<QMessageBox *>(modal);
            unexpected.append(box != nullptr ? box->text() : modal->windowTitle());
            last_ = modal;
            QTimer::singleShot(0, modal, &QWidget::close);
            return;
        }
        for (QAbstractButton *button : modal->findChildren<QAbstractButton *>()) {
            if (button->isVisible() && button->isEnabled() && plain(button->text()) == buttons_.front()) {
                if (auto *box = qobject_cast<QMessageBox *>(modal))
                    seen.append(box->text());
                buttons_.pop_front();
                last_ = button->text() == QStringLiteral("Close") ? modal : nullptr;
                QTimer::singleShot(0, button, &QAbstractButton::click);
                return;
            }
        }
    }

    std::deque<QString> buttons_;
    QTimer timer_;
    QWidget *last_ = nullptr;
};

}
