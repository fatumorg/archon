#pragma once

#include "core/meta/text_file.h"

#include <QDialog>

#include <functional>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace archon {

class LineNumbers;

class TextEditor : public QDialog {
    Q_OBJECT
public:
    using Save = std::function<Result<void>(const std::vector<std::byte> &file)>;

    TextEditor(const QString &name, TextFile file, bool editable, Save save, QWidget *parent = nullptr);

    static QString lineLabel(int line);
    void showReadOnly(const QString &reason);
    bool save();
    void reject() override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    friend class LineNumbers;
    void paintLineNumbers(QPaintEvent *event);
    void updateMargins();

    TextFile file_;
    bool editable_;
    bool modified_ = false;
    Save save_;
    QPlainTextEdit *text_ = nullptr;
    LineNumbers *numbers_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QLabel *status_ = nullptr;
};

}
