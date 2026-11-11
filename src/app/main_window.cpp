#include "app/main_window.h"

namespace archon {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ARCHON"));
    resize(1280, 800);
}

}
