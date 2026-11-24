#include <QApplication>
#include <QIcon>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/icons/workdesk.ico"));
    QCoreApplication::setOrganizationName(QStringLiteral("WorkDesk"));
    QCoreApplication::setApplicationName(QStringLiteral("WorkDesk"));
    QApplication::setApplicationDisplayName(QStringLiteral("WorkDesk"));

    MainWindow window;
    window.showMaximized();
    return app.exec();
}
