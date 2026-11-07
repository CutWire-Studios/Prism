#include "app/AppController.h"
#include "app/show/CueThumbnails.h"
#include "app/show/ShowManager.h"
#include "app/GraphicsApiChoice.h"
#include "app/gpu/GpuPreference.h"
#include "app/graph/GraphRuntime.h"
#include "app/preview/AppCompositor.h"
#include "core/platform/MacPermissions.h"
#include "core/render/TextLayout.h"
#include "mcp/McpStdio.h"
#include "core/media/GpuVideoUploader.h"
#include "core/media/VideoDecoder.h"
#include "release.h"
#include "app/editor/PreferencesController.h"

#include <QByteArray>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QTimer>
#include <QtGlobal>
#include <QCoreApplication>

extern "C" {
#include <libavutil/log.h>
}

#ifdef Q_OS_WIN
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;
}
#endif

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--mcp-stdio") == 0) {
            QCoreApplication app(argc, argv);
            QCoreApplication::setOrganizationName(QStringLiteral("Prism"));
            QCoreApplication::setApplicationName(QStringLiteral(PRISM_APP_NAME));
            return prism::mcp::runStdioAttach();
        }
    }

    // Quiet libav's container quirk spam (e.g. "Referenced QT chapter track not
    // found") which is harmless; keep genuine errors visible.
    av_log_set_level(AV_LOG_ERROR);

#ifdef Q_OS_LINUX
    // Route file dialogs through the xdg-desktop-portal chooser, as the Flatpak build already
    // does, unless the user or distro chose a platform theme.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif

    // Let offscreen GL contexts (e.g. SlideshowSource's transition FBO) share
    // textures with the preview scene-graph contexts, so rendered frames
    // can be drawn directly without a GPU→CPU→GPU readback.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // Settings are read before QGuiApplication exists: VAAPI zero-copy needs Qt's xcb plugin on
    // EGL, which has to be chosen before the platform plugin loads.
    QCoreApplication::setOrganizationName(QStringLiteral("Prism"));
    QCoreApplication::setApplicationName(QStringLiteral(PRISM_APP_NAME));
    [[maybe_unused]] const bool graphicsApiGuarded = prism::graphics::applyAtStartup(argc, argv);
    prism::applyZeroCopyXcbEgl();
#ifdef Q_OS_WIN
    if (prism::gpu::applyStoredPreference() == prism::gpu::Preference::HighPerformance) {
        NvOptimusEnablement = 1;
        AmdPowerXpressRequestHighPerformance = 1;
    }
#endif
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QGuiApplication app(argc, argv);
    app.setOrganizationName("Prism");
    app.setApplicationName(PRISM_APP_NAME);
    VideoDecoder::applyModeSetting(VideoDecoder::modeSetting());
    // Resources must be registered before using :/ icons (Windows has no theme icon).
    Q_INIT_RESOURCE(resources);
    PreferencesController::installUiTranslators();
    // Wayland compositors resolve the window icon by matching the surface's
    // app_id to an installed .desktop file. Qt derives app_id from the desktop
    // file name, so this must equal the installed desktop file (named after PRISM_APP_ID) or
    // the window falls back to the generic icon. setWindowIcon covers X11.
    app.setDesktopFileName(QStringLiteral(PRISM_APP_ID));
    {
        QIcon appIcon = QIcon::fromTheme(QStringLiteral(PRISM_APP_ID));
        if (appIcon.isNull())
            appIcon = QIcon(QStringLiteral(":/Prism_icon.png"));
        app.setWindowIcon(appIcon);
    }
    prism::text::registerBundledFonts();
    {
        QFont f = app.font();
        f.setFamily(QStringLiteral("Inter"));
        f.setPixelSize(13);
        app.setFont(f);
    }

    AppController controller(nullptr);
    AppCompositor::setInstance(controller.compositor());

    QQmlApplicationEngine engine;
    controller.setEngine(&engine);
    engine.addImageProvider(QStringLiteral("nodethumb"), controller.showManager()->createImageProvider());
    engine.addImageProvider(QStringLiteral("cuethumb"), CueThumbnails::createProvider(controller.showManager()));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
                     [&controller] { controller.shutdown(); });
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Prism/src/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        if (graphicsApiGuarded) {
            QObject::connect(window, &QQuickWindow::frameSwapped, &app,
                             [] { prism::graphics::markStarted(); },
                             static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
            QObject::connect(window, &QQuickWindow::sceneGraphError, &app,
                             [](QQuickWindow::SceneGraphError, const QString &message) {
                qCritical("Qt Quick could not start on the chosen graphics API: %s", qUtf8Printable(message));
                prism::graphics::revertAfterFailure();
                QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                        QCoreApplication::arguments().mid(1));
                QCoreApplication::exit(1);
            }, Qt::QueuedConnection);
        }
        if (qEnvironmentVariableIsSet("PRISM_AUTO_QUIT_MS")) {
            const int ms = qEnvironmentVariableIntValue("PRISM_AUTO_QUIT_MS");
            QTimer::singleShot(qMax(ms, 1), window, &QQuickWindow::close);
        }
    }

    controller.start();

    // macOS shows the camera/mic prompt only when the app asks explicitly. No-op elsewhere.
    MacPermissions::requestCameraAndMicrophone();

    return app.exec();
}
