#include <QApplication>
#include <QByteArray>
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    // WebEngine / Chromium uses the Gallium GPU stack on this system which
    // crashes when trying to initialize its Vulkan/GBM render path on Wayland.
    // Force software rendering to avoid the libgallium SIGSEGV.
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");

    // Let offscreen GL contexts (e.g. SlideshowSource's transition FBO) share
    // textures with the VideoWidget QOpenGLWidget contexts, so rendered frames
    // can be drawn directly without a GPU→CPU→GPU readback.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // Resources are compiled into switchx_core (static lib); register them here so
    // :/… paths (shaders, HTML presets, Lua examples, etc.) resolve at runtime.
    Q_INIT_RESOURCE(resources);

    QApplication app(argc, argv);
    app.setOrganizationName("SwitchX");
    app.setApplicationName("SwitchX");
    app.setStyle("fusion");

    MainWindow window;
    window.show();

    return app.exec();
}
