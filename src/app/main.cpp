#include "app/main_window.h"
#include "app/style.h"
#include "core/version.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ARCHON"));
    QApplication::setApplicationVersion(archon::versionString());
    QApplication::setDesktopFileName(QStringLiteral("archon"));
    archon::applyStyle(application);
    const QStringList arguments = QApplication::arguments();
    archon::MainWindow window(std::filesystem::path(arguments.value(1).toStdString()), nullptr);
    window.show();
    return QApplication::exec();
}
