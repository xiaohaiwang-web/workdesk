#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WorkDesk"));
    QCoreApplication::setApplicationName(QStringLiteral("WorkDesk"));
    QApplication::setApplicationDisplayName(QStringLiteral("WorkDesk"));

    MainWindow window;
    window.showMaximized();
    return app.exec();
}
