// main.cpp
#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Identify the app so QSettings (used to persist the custom spell-check
    // dictionary) writes to a stable, app-specific location.
    QCoreApplication::setOrganizationName("MattWord");
    QCoreApplication::setApplicationName("MattWord");

    MainWindow window;
    window.show();
    return app.exec();
}
