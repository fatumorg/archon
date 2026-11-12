#include "app/text_editor.h"
#include "support/modal_driver.h"
#include "support/test_bytes.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>

using namespace archon;
using namespace archon::test;

class TestTextEditor : public QObject {
    Q_OBJECT
private slots:
    void showsTextReadOnly();
    void editsAndSavesKeepingLineEndings();
    void closingWithChangesAsks();
};

namespace {

TextFile crlfFile()
{
    return *decodeTextFile(toVector("<a>\r\n\t<b/>\r\n</a>\r\n"), "x.meta");
}

}

void TestTextEditor::showsTextReadOnly()
{
    TextEditor editor(QStringLiteral("handling.meta"), crlfFile(), false, [](const std::vector<std::byte> &) -> Result<void> { return {}; });
    editor.show();
    QVERIFY(editor.windowTitle().startsWith(QStringLiteral("handling.meta - ARCHON")));
    auto *text = editor.findChild<QPlainTextEdit *>(QStringLiteral("textEdit"));
    QCOMPARE(text->toPlainText(), QStringLiteral("<a>\n\t<b/>\n</a>\n"));
    QVERIFY(text->isReadOnly());
    QVERIFY(!editor.findChild<QPushButton *>(QStringLiteral("saveButton"))->isEnabled());
    QCOMPARE(editor.findChild<QLabel *>(QStringLiteral("editorStatus"))->text(), QStringLiteral("Read only: turn on edit mode to change this file."));
    QCOMPARE(TextEditor::lineLabel(7), QStringLiteral("0007"));
    QCOMPARE(TextEditor::lineLabel(12345), QStringLiteral("12345"));
    QVERIFY(editor.findChild<QWidget *>(QStringLiteral("lineNumbers"))->width() > 0);
}

void TestTextEditor::editsAndSavesKeepingLineEndings()
{
    std::vector<std::byte> saved;
    TextEditor editor(QStringLiteral("x.meta"), crlfFile(), true, [&saved](const std::vector<std::byte> &bytes) -> Result<void> {
        saved = bytes;
        return {};
    });
    editor.show();
    auto *text = editor.findChild<QPlainTextEdit *>(QStringLiteral("textEdit"));
    auto *save = editor.findChild<QPushButton *>(QStringLiteral("saveButton"));
    QVERIFY(!text->isReadOnly());
    QVERIFY(!save->isEnabled());
    text->setPlainText(QStringLiteral("<a>\n\t<c/>\n</a>\n"));
    QVERIFY(save->isEnabled());
    QVERIFY(editor.save());
    QCOMPARE(toText(saved), std::string("<a>\r\n\t<c/>\r\n</a>\r\n"));
    QVERIFY(!save->isEnabled());
}

void TestTextEditor::closingWithChangesAsks()
{
    TextEditor editor(QStringLiteral("x.meta"), crlfFile(), true, [](const std::vector<std::byte> &) -> Result<void> { return {}; });
    editor.show();
    editor.findChild<QPlainTextEdit *>(QStringLiteral("textEdit"))->setPlainText(QStringLiteral("changed"));
    {
        ModalDriver driver({ QStringLiteral("Cancel") });
        editor.reject();
        QVERIFY(driver.done());
    }
    QVERIFY(editor.isVisible());
    ModalDriver driver({ QStringLiteral("Discard") });
    editor.reject();
    QVERIFY(driver.done());
    QVERIFY(!editor.isVisible());
}

QTEST_MAIN(TestTextEditor)
#include "tst_text_editor.moc"
