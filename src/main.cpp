#include <QApplication>
#include <QByteArray>
#include <QTimer>
#include <QtGlobal>
#include "ui/mainwindow/MainWindow.h"
#include "ui/common/MaterialSymbols.h"

extern "C" {
#include <libavutil/log.h>
}

int main(int argc, char *argv[]) {
    // Quiet libav's container quirk spam (e.g. "Referenced QT chapter track not
    // found") which is harmless; keep genuine errors visible.
    av_log_set_level(AV_LOG_ERROR);

    // WebEngine / Chromium uses the Gallium GPU stack on this system which
    // crashes when trying to initialize its Vulkan/GBM render path on Wayland.
    // Force software rendering to avoid the libgallium SIGSEGV.
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");

    // Let offscreen GL contexts (e.g. SlideshowSource's transition FBO) share
    // textures with the VideoWidget QOpenGLWidget contexts, so rendered frames
    // can be drawn directly without a GPU→CPU→GPU readback.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // Resources are compiled into prism_core (static lib); register them here so
    // :/… paths (shaders, HTML presets, Lua examples, etc.) resolve at runtime.
    Q_INIT_RESOURCE(resources);

    QApplication app(argc, argv);
    MaterialSymbols::init();
    app.setOrganizationName("Prism");
    app.setApplicationName("Prism");
    app.setStyle("fusion");

    MainWindow window;
    window.show();

    if (qEnvironmentVariableIsSet("PRISM_AUTO_QUIT_MS")) {
        const int ms = qEnvironmentVariableIntValue("PRISM_AUTO_QUIT_MS");
        QTimer::singleShot(qMax(ms, 1), &window, &QWidget::close);
    }

    return app.exec();
}
