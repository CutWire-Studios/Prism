#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/GetStartedDialog.h"
#include "ui/common/Icons.h"
#include "ui/common/Theme.h"
#include "ui/mainwindow/PrismSplashScreen.h"
#include "core/platform/MacPermissions.h"
#include "mcp/McpStdio.h"
#include "core/media/GpuVideoUploader.h"
#include "core/media/VideoDecoder.h"

#include <QApplication>
#include <QByteArray>
#include <QIcon>
#include <QTimer>
#include <QtGlobal>
#include <QThread>
#include <QCoreApplication>

extern "C" {
#include <libavutil/log.h>
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--mcp-stdio") == 0) {
            QCoreApplication app(argc, argv);
            QCoreApplication::setOrganizationName(QStringLiteral("Prism"));
            QCoreApplication::setApplicationName(QStringLiteral("Prism"));
            return prism::mcp::runStdioAttach();
        }
    }

    // Quiet libav's container quirk spam (e.g. "Referenced QT chapter track not
    // found") which is harmless; keep genuine errors visible.
    av_log_set_level(AV_LOG_ERROR);

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

    // Settings are read before QApplication exists: VAAPI zero-copy needs Qt's xcb plugin on
    // EGL, which has to be chosen before the platform plugin loads.
    QCoreApplication::setOrganizationName(QStringLiteral("Prism"));
    QCoreApplication::setApplicationName(QStringLiteral("Prism"));
    prism::applyZeroCopyXcbEgl();

    QApplication app(argc, argv);
    app.setOrganizationName("Prism");
    app.setApplicationName("Prism");
    VideoDecoder::applyModeSetting(VideoDecoder::modeSetting());
    // Resources must be registered before using :/ icons (Windows has no theme icon).
    Q_INIT_RESOURCE(resources);
    // Wayland compositors resolve the window icon by matching the surface's
    // app_id to an installed .desktop file. Qt derives app_id from the desktop
    // file name, so this must equal the installed org.cutwire.Prism.desktop or
    // the window falls back to the generic icon. setWindowIcon covers X11.
    app.setDesktopFileName(QStringLiteral("org.cutwire.Prism"));
    {
        QIcon appIcon = QIcon::fromTheme(QStringLiteral("org.cutwire.Prism"));
        if (appIcon.isNull())
            appIcon = QIcon(QStringLiteral(":/Prism_icon.png"));
        app.setWindowIcon(appIcon);
    }
    Icons::init();
    Theme::loadFonts();
    Theme::instance().apply();

    // Initialize and show custom splash screen
    PrismSplashScreen splash;
    splash.show();
    splash.setProgress(15, "Initializing video codecs...");
    app.processEvents();
    QThread::msleep(150);

    // Resources are compiled into prism_core (static lib); register them here so
    // :/… paths (shaders, SVG templates, Lua examples, etc.) resolve at runtime.
    splash.setProgress(45, "Loading icons & resources...");
    app.processEvents();
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
    GetStartedDialog::showIfNeeded(&window);

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
