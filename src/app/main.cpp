#include "app/main_window.h"
#include "core/version.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ARCHON"));
    QApplication::setApplicationVersion(archon::versionString());
    QApplication::setDesktopFileName(QStringLiteral("archon"));
    archon::MainWindow window;
    window.show();
    return QApplication::exec();
}
