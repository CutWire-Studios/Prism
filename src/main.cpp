#include <QApplication>
#include <QByteArray>
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    // WebEngine / Chromium uses the Gallium GPU stack on this system which
    // crashes when trying to initialize its Vulkan/GBM render path on Wayland.
    // Force software rendering to avoid the libgallium SIGSEGV.
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");

    QApplication app(argc, argv);
    app.setStyle("fusion");

    MainWindow window;
    window.show();

    return app.exec();
}
