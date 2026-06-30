#include "ui/output/VirtualCameraProgramSink.h"
#include "ui/canvas/VideoWidget.h"

#include <QDebug>
#include <QSettings>

#include <softcam.h>

namespace {

constexpr float kRealtimeFramerate = 0.0f;
constexpr QLatin1StringView kSoftcamDeviceName("DirectShow Softcam");

void rgbaToBgrBottomUp(const QImage &rgba, QVector<uint8_t> &bgr) {
    const int width  = rgba.width();
    const int height = rgba.height();
    const qsizetype rowBytes = static_cast<qsizetype>(width) * 3;
    bgr.resize(rowBytes * height);

    for (int y = 0; y < height; ++y) {
        const uint8_t *src = rgba.constScanLine(y);
        uint8_t *dst       = bgr.data() + static_cast<qsizetype>(height - 1 - y) * rowBytes;

        for (int x = 0; x < width; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 2];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 0];
        }
    }
}

} // namespace

struct VirtualCameraProgramSink::Impl {
    scCamera camera = nullptr;
    int width = 0;
    int height = 0;
    QVector<uint8_t> bgrBuffer;
};

VirtualCameraProgramSink::VirtualCameraProgramSink()
    : m_impl(std::make_unique<Impl>())
    , m_devicePath(defaultDevicePath())
{
}

VirtualCameraProgramSink::~VirtualCameraProgramSink() {
    stopInternal();
}

bool VirtualCameraProgramSink::isAvailable() const {
    return true;
}

QStringList VirtualCameraProgramSink::availableLoopbackDevices() {
    return {};
}

QString VirtualCameraProgramSink::defaultDevicePath() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("virtualCamera"));
    const QString saved = settings.value(QStringLiteral("devicePath")).toString();
    settings.endGroup();

    if (!saved.isEmpty())
        return saved;

    return QString(kSoftcamDeviceName);
}

bool VirtualCameraProgramSink::start(const QString &streamName) {
    Q_UNUSED(streamName);
    stop();

    if (m_devicePath.isEmpty())
        m_devicePath = defaultDevicePath();

    m_impl->width  = VideoWidget::kProgramWidth;
    m_impl->height = VideoWidget::kProgramHeight;

    m_impl->camera = scCreateCamera(m_impl->width, m_impl->height, kRealtimeFramerate);
    if (!m_impl->camera) {
        qWarning() << "VirtualCamera: scCreateCamera failed — another app may already"
                   << "own the DirectShow Softcam instance";
        return false;
    }

    m_impl->bgrBuffer.resize(static_cast<qsizetype>(m_impl->width) * m_impl->height * 3);
    m_active = true;

    QSettings settings;
    settings.beginGroup(QStringLiteral("virtualCamera"));
    settings.setValue(QStringLiteral("devicePath"), m_devicePath);
    settings.endGroup();

    qDebug() << "VirtualCamera: streaming program output to" << kSoftcamDeviceName
             << m_impl->width << "x" << m_impl->height << "BGR";
    return true;
}

void VirtualCameraProgramSink::stopInternal() {
    if (m_impl->camera) {
        scDeleteCamera(m_impl->camera);
        m_impl->camera = nullptr;
    }

    m_impl->width  = 0;
    m_impl->height = 0;
    m_active       = false;
    m_impl->bgrBuffer.clear();
}

void VirtualCameraProgramSink::submitFrame(const QImage &frame) {
    if (!m_active || !m_impl->camera || frame.isNull())
        return;

    QImage rgba = frame;
    if (rgba.format() != QImage::Format_RGBA8888)
        rgba = rgba.convertToFormat(QImage::Format_RGBA8888);

    if (rgba.width() != m_impl->width || rgba.height() != m_impl->height) {
        rgba = rgba.scaled(m_impl->width, m_impl->height,
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    rgbaToBgrBottomUp(rgba, m_impl->bgrBuffer);
    scSendFrame(m_impl->camera, m_impl->bgrBuffer.constData());
}
