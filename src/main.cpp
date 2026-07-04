#include <QApplication>
#include <QByteArray>
#include <QIcon>
#include <QTimer>
#include <QtGlobal>
#include <QThread>
#include "ui/mainwindow/MainWindow.h"
#include "ui/common/MaterialSymbols.h"
#include "ui/mainwindow/PrismSplashScreen.h"
#include "core/platform/MacPermissions.h"

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

#ifdef Q_OS_LINUX
    // The app themes itself with a global qApp stylesheet. On a normal desktop
    // (non-Flatpak) QFileDialog uses the platform theme's *in-process* native
    // dialog (e.g. KDE/GTK), so that stylesheet bleeds into it and produces a
    // broken, half-styled file picker. Routing dialogs through the out-of-process
    // xdg-desktop-portal chooser leaves them as the clean, unstyled native picker
    // — the same path the Flatpak build already takes. Only set when the user or
    // distro hasn't chosen a platform theme themselves.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif

    // Let offscreen GL contexts (e.g. SlideshowSource's transition FBO) share
    // textures with the VideoWidget QOpenGLWidget contexts, so rendered frames
    // can be drawn directly without a GPU→CPU→GPU readback.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    app.setOrganizationName("Prism");
    app.setApplicationName("Prism");
    // Wayland compositors resolve the window icon by matching the surface's
    // app_id to an installed .desktop file. Qt derives app_id from the desktop
    // file name, so this must equal the installed org.cutwire.Prism.desktop or
    // the window falls back to the generic icon. setWindowIcon covers X11.
    app.setDesktopFileName(QStringLiteral("org.cutwire.Prism"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("org.cutwire.Prism")));
    app.setStyle("fusion");

    // Initialize and show custom splash screen
    PrismSplashScreen splash;
    splash.show();
    splash.setProgress(15, "Initializing video codecs...");
    app.processEvents();
    QThread::msleep(150);

    // Resources are compiled into prism_core (static lib); register them here so
    // :/… paths (shaders, HTML presets, Lua examples, etc.) resolve at runtime.
    splash.setProgress(45, "Loading Material Symbols & resources...");
    app.processEvents();
    Q_INIT_RESOURCE(resources);
    MaterialSymbols::init();
    QThread::msleep(150);

    splash.setProgress(75, "Constructing live media engine...");
    app.processEvents();
    MainWindow window;
    QThread::msleep(150);

    splash.setProgress(100, "Starting interface...");
    app.processEvents();
    QThread::msleep(100);

    window.show();
    splash.finish(&window);

    // macOS shows the camera/mic prompt only when the app asks explicitly. Do it
    // once the window is up so the dialog isn't parented to the splash. No-op
    // elsewhere.
    MacPermissions::requestCameraAndMicrophone();

    if (qEnvironmentVariableIsSet("PRISM_AUTO_QUIT_MS")) {
        const int ms = qEnvironmentVariableIntValue("PRISM_AUTO_QUIT_MS");
        QTimer::singleShot(qMax(ms, 1), &window, &QWidget::close);
    }

    return app.exec();
}
