#include "app/game_setup.h"
#include "app/history_dialog.h"
#include "app/main_window.h"
#include "app/style.h"
#include "core/version.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ARCHON"));
    QApplication::setApplicationVersion(archon::versionString());
    QApplication::setDesktopFileName(QStringLiteral("archon"));
    archon::applyStyle(application);
    const auto session = archon::openSession(nullptr);
    if (!session)
        return 1;
    const auto lock = archon::lockStore(session->store);
    if (!lock) {
        QMessageBox::critical(nullptr, QStringLiteral("ARCHON"), QObject::tr("ARCHON is already open for this game folder."));
        return 1;
    }
    if (!archon::recoverUnfinished(session->store, nullptr))
        return 0;
    archon::MainWindow window(session->install.root, session->keys, session->store);
    window.show();
    return QApplication::exec();
}
