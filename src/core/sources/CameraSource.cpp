#include "core/sources/CameraSource.h"
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QMediaDevices>
#include <QApplication>
#include <QMessageBox>
#include <QHash>
#include <QDebug>

namespace {

QString friendlyCameraError(QCamera::Error err, const QString &msg) {
    Q_UNUSED(err);
    if (msg.contains(QStringLiteral("general stream error"), Qt::CaseInsensitive)
        || msg.contains(QStringLiteral("GStreamer"), Qt::CaseInsensitive)) {
        return QObject::tr(
            "The camera could not start.\n\n"
            "Common causes:\n"
            "• Another app is using the camera (OBS, browser, Zoom, etc.)\n"
            "• The selected device path is wrong — try \"Default Camera\"\n"
            "• The camera driver needs a moment after unplug/replug\n\n"
            "Technical detail: %1").arg(msg);
    }
    return msg.isEmpty()
        ? QObject::tr("The camera could not start.")
        : msg;
}

}

// ── Shared device backend ───────────────────────────────────────────────────────
// Owns the QCamera/session/sink for one physical device and holds the latest
// decoded frame. Shared between every CameraSource pointing at the same device;
// the device is opened on the first acquire and closed when the last consumer
// drops its reference.
class CameraBackend {
public:
    CameraBackend(const QCameraDevice &device, QString name)
        : m_name(std::move(name)) {
        m_camera  = device.isNull() ? new QCamera() : new QCamera(device);
        m_session = new QMediaCaptureSession();
        m_sink    = new QVideoSink();

        // Do NOT call setCameraFormat() — on Intel IPU6 / MIPI cameras forcing a
        // format causes "poll error: Invalid argument". Let GStreamer auto-negotiate.
        m_session->setCamera(m_camera);
        m_session->setVideoSink(m_sink);

        // Frames arrive on the multimedia backend thread; marshal to the sink's
        // (main) thread so consumers can read m_frame without locking.
        QObject::connect(m_sink, &QVideoSink::videoFrameChanged, m_sink,
            [this](const QVideoFrame &frame) {
                if (!frame.isValid()) return;
                QImage img = frame.toImage().convertToFormat(QImage::Format_RGB888);
                if (!img.isNull()) {
                    m_frame = std::move(img);
                    ++m_gen;
                }
            }, Qt::QueuedConnection);

        QObject::connect(m_camera, &QCamera::errorOccurred, m_sink,
            [this](QCamera::Error err, const QString &msg) {
                m_lastError = friendlyCameraError(err, msg);
                m_failed = true;
                qWarning() << "CameraBackend error:" << err << msg;
                // Defer: stopping the camera or popping a modal dialog from inside
                // its own errorOccurred emission is unsafe. m_sink is the context,
                // so the queued call is dropped if this backend is destroyed first.
                QMetaObject::invokeMethod(m_sink, [this]() {
                    if (m_camera) m_camera->stop();
                    if (QWidget *w = QApplication::activeWindow())
                        QMessageBox::warning(w, QObject::tr("Camera Error"), m_lastError);
                }, Qt::QueuedConnection);
            });

        m_camera->start();
        qDebug() << "CameraBackend: started" << m_name;
    }

    ~CameraBackend() {
        if (m_camera)  m_camera->stop();
        if (m_session) {
            m_session->setCamera(nullptr);
            m_session->setVideoSink(nullptr);
        }
        delete m_sink;    m_sink    = nullptr;
        delete m_session; m_session = nullptr;
        delete m_camera;  m_camera  = nullptr;
    }

    const QImage &frame()   const { return m_frame; }
    quint64  generation()   const { return m_gen; }
    bool     failed()       const { return m_failed; }
    QString  lastError()    const { return m_lastError; }

private:
    QCamera              *m_camera  = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QVideoSink           *m_sink    = nullptr;

    QImage  m_frame;             // Format_RGB888, updated on the main thread
    quint64 m_gen    = 0;        // bumped on every new frame
    bool    m_failed = false;
    QString m_lastError;
    QString m_name;
};

namespace {

// Live backends keyed by device. weak_ptr so a backend is destroyed (and its
// camera closed) as soon as the last CameraSource referencing it is gone.
QHash<QString, std::weak_ptr<CameraBackend>> g_backends;

std::shared_ptr<CameraBackend> acquireBackend(const QString &key,
                                              const QCameraDevice &device,
                                              const QString &name) {
    if (auto it = g_backends.find(key); it != g_backends.end()) {
        if (auto sp = it->lock())
            return sp;   // reuse the already-open device
    }
    auto sp = std::make_shared<CameraBackend>(device, name);
    g_backends.insert(key, sp);
    return sp;
}

}

// ── CameraSource ────────────────────────────────────────────────────────────────

CameraSource::CameraSource() = default;

CameraSource::~CameraSource() {
    stop();
}

bool CameraSource::start(const QCameraDevice &device) {
    stop();

    // If a valid device was provided, use it; otherwise let Qt pick the default.
    // Deliberately do NOT call QMediaDevices::defaultVideoInput() here — on some
    // hardware (Intel IPU6 / MIPI cameras) QMediaDevices::videoInputs() returns
    // empty even though QCamera() with no arguments can still access the camera.
    QString key;
    if (!device.isNull()) {
        m_name = device.description().isEmpty()
               ? QString::fromUtf8(device.id())
               : device.description();
        key = QStringLiteral("id:") + QString::fromUtf8(device.id());
    } else {
        m_name = "Default Camera";
        key = QStringLiteral("@default");
    }

    m_backend = acquireBackend(key, device, m_name);
    m_lastGen = 0;
    m_frame   = {};
    return true;  // starting is async — success is confirmed when frames arrive
}

bool CameraSource::startDevice(const QString &devicePath) {
    const auto all = QMediaDevices::videoInputs();
    for (const auto &dev : all) {
        const QString id = QString::fromUtf8(dev.id());
        if (id == devicePath)
            return start(dev);
#ifdef Q_OS_WIN
        // Media Foundation symbolic links may appear with or without a @device: prefix.
        if (!devicePath.isEmpty()) {
            const QString normalized = id.startsWith(QStringLiteral("@device:"))
                ? id.mid(8) : id;
            const QString pathNorm = devicePath.startsWith(QStringLiteral("@device:"))
                ? devicePath.mid(8) : devicePath;
            if (normalized.compare(pathNorm, Qt::CaseInsensitive) == 0)
                return start(dev);
        }
#endif
    }
    // No Qt device matched the path — fall back to QCamera() with no device
    // argument (system default).  This works even for Intel IPU6 / MIPI cameras
    // where QMediaDevices::videoInputs() returns empty.
    qDebug() << "CameraSource: path" << devicePath
             << "not in Qt device list — using QCamera() default";
    return start({});   // {} = null QCameraDevice → QCamera() with no device
}

void CameraSource::stop() {
    m_backend.reset();
    m_frame   = {};
    m_lastGen = 0;
}

bool CameraSource::nextFrame() {
    if (!m_backend) return false;
    const quint64 gen = m_backend->generation();
    if (gen == m_lastGen) return false;
    m_lastGen = gen;
    m_frame   = m_backend->frame();   // cheap: QImage is implicitly shared
    return true;
}

bool CameraSource::isReady() const {
    return m_backend && !m_backend->failed() && !m_frame.isNull();
}

bool CameraSource::hasFailed() const {
    return m_backend && m_backend->failed();
}

QString CameraSource::lastError() const {
    return m_backend ? m_backend->lastError() : QString();
}
