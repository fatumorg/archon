#include "app/main_window.h"

#include <QAbstractButton>
#include <QAction>
#include <QTest>

class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    void showsTitleAndNoDeadControls();
};

void TestMainWindow::showsTitleAndNoDeadControls()
{
    archon::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(window.windowTitle(), QStringLiteral("ARCHON"));
    QVERIFY(window.findChildren<QAction *>().isEmpty());
    QVERIFY(window.findChildren<QAbstractButton *>().isEmpty());
}

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
