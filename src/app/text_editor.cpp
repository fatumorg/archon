#include "app/text_editor.h"

#include "core/version.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QVBoxLayout>

namespace archon {

class LineNumbers : public QWidget {
public:
    explicit LineNumbers(TextEditor *editor, QWidget *parent)
        : QWidget(parent)
        , editor_(editor)
    {
        setObjectName(QStringLiteral("lineNumbers"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        editor_->paintLineNumbers(event);
    }

private:
    TextEditor *editor_;
};

namespace {

class GutterEdit : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    using QPlainTextEdit::blockBoundingGeometry;
    using QPlainTextEdit::blockBoundingRect;
    using QPlainTextEdit::contentOffset;
    using QPlainTextEdit::firstVisibleBlock;
    using QPlainTextEdit::setViewportMargins;
};

GutterEdit *gutter(QPlainTextEdit *edit)
{
    return static_cast<GutterEdit *>(edit);
}

}

TextEditor::TextEditor(const QString &name, TextFile file, bool editable, Save save, QWidget *parent)
    : QDialog(parent)
    , file_(std::move(file))
    , editable_(editable)
    , save_(std::move(save))
{
    setWindowTitle(QStringLiteral("%1 - ARCHON %2").arg(name, versionString()));
    resize(860, 640);

    text_ = new GutterEdit(this);
    text_->setObjectName(QStringLiteral("textEdit"));
    text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    text_->setPlainText(file_.text);
    text_->setReadOnly(!editable_);
    numbers_ = new LineNumbers(this, text_);
    connect(text_, &QPlainTextEdit::blockCountChanged, this, &TextEditor::updateMargins);
    connect(text_, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy != 0)
            numbers_->scroll(0, dy);
        else
            numbers_->update(0, rect.y(), numbers_->width(), rect.height());
    });
    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        modified_ = true;
        saveButton_->setEnabled(editable_);
    });

    status_ = new QLabel(editable_ ? tr("Editing") : tr("Read only: turn on edit mode to change this file."), this);
    status_->setObjectName(QStringLiteral("editorStatus"));
    saveButton_ = new QPushButton(tr("Save"), this);
    saveButton_->setObjectName(QStringLiteral("saveButton"));
    saveButton_->setEnabled(false);
    auto *close = new QPushButton(tr("Close"), this);
    connect(saveButton_, &QPushButton::clicked, this, &TextEditor::save);
    connect(close, &QPushButton::clicked, this, &TextEditor::reject);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(status_, 1);
    buttons->addWidget(saveButton_);
    buttons->addWidget(close);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(text_, 1);
    layout->addLayout(buttons);
    updateMargins();
}

QString TextEditor::lineLabel(int line)
{
    return QStringLiteral("%1").arg(line, 4, 10, QLatin1Char('0'));
}

void TextEditor::showReadOnly(const QString &reason)
{
    editable_ = false;
    text_->setReadOnly(true);
    saveButton_->setEnabled(false);
    status_->setText(reason);
}

void TextEditor::updateMargins()
{
    const int digits = std::max(4, static_cast<int>(QString::number(text_->blockCount()).size()));
    const int width = 8 + text_->fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    gutter(text_)->setViewportMargins(width, 0, 0, 0);
    const QRect area = text_->contentsRect();
    numbers_->setGeometry(area.left(), area.top(), width, area.height());
}

void TextEditor::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateMargins();
}

void TextEditor::paintLineNumbers(QPaintEvent *event)
{
    QPainter painter(numbers_);
    painter.fillRect(event->rect(), QColor(0xF0, 0xF0, 0xF0));
    painter.setFont(text_->font());
    painter.setPen(QColor(0x2B, 0x91, 0xAF));
    QTextBlock block = gutter(text_)->firstVisibleBlock();
    int top = static_cast<int>(gutter(text_)->blockBoundingGeometry(block).translated(gutter(text_)->contentOffset()).top());
    const int lineHeight = text_->fontMetrics().height();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible())
            painter.drawText(0, top, numbers_->width() - 4, lineHeight, Qt::AlignRight, lineLabel(block.blockNumber() + 1));
        top += static_cast<int>(gutter(text_)->blockBoundingRect(block).height());
        block = block.next();
    }
}

bool TextEditor::save()
{
    if (!editable_ || !modified_)
        return false;
    const auto encoded = encodeTextFile(file_, text_->toPlainText());
    if (!encoded) {
        QMessageBox::warning(this, QStringLiteral("ARCHON"), QString::fromStdString(encoded.error().message));
        return false;
    }
    const auto result = save_(*encoded);
    if (!result) {
        if (!result.error().message.empty())
            QMessageBox::warning(this, QStringLiteral("ARCHON"), QString::fromStdString(result.error().message));
        return false;
    }
    modified_ = false;
    saveButton_->setEnabled(false);
    return true;
}

void TextEditor::reject()
{
    if (modified_ && editable_) {
        const auto answer = QMessageBox::question(this, QStringLiteral("ARCHON"), tr("Save the changes to this file?"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save()))
            return;
    }
    QDialog::reject();
}

}
