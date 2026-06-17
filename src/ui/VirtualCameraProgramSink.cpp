#include "ui/VirtualCameraProgramSink.h"
#include "ui/VideoWidget.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSettings>

#include <algorithm>
#include <cstring>

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

#ifdef __linux__
constexpr int kFrameRateNum = 30;

bool queryOutputCapability(int fd) {
    v4l2_capability cap{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
        return false;

    const __u32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                     ? cap.device_caps
                     : cap.capabilities;
    return (caps & V4L2_CAP_VIDEO_OUTPUT) != 0;
}

bool configureOutputFormat(int fd, int width, int height, int &outWidth, int &outHeight) {
    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width       = static_cast<__u32>(width);
    fmt.fmt.pix.height      = static_cast<__u32>(height);
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
        return false;

    outWidth  = static_cast<int>(fmt.fmt.pix.width);
    outHeight = static_cast<int>(fmt.fmt.pix.height);
    return fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV;
}

bool startOutputStream(int fd) {
    const v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    return ioctl(fd, VIDIOC_STREAMON, &type) == 0;
}

void stopOutputStream(int fd) {
    const v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(fd, VIDIOC_STREAMOFF, &type);
}

uint8_t clampByte(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

void rgbaToYuyv(const uint8_t *rgba, int width, int height, int rgbaStride,
                uint8_t *yuyv) {
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = rgba + y * rgbaStride;
        uint8_t *out       = yuyv + y * width * 2;

        for (int x = 0; x < width; x += 2) {
            const auto sample = [&](int px) {
                const uint8_t *p = row + px * 4;
                const int r = p[0], g = p[1], b = p[2];
                const int yVal = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
                const int uVal = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                const int vVal = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                return std::tuple{yVal, uVal, vVal};
            };

            const auto [y0, u0, v0] = sample(x);
            int y1 = y0, u1 = u0, v1 = v0;
            if (x + 1 < width) {
                std::tie(y1, u1, v1) = sample(x + 1);
            }

            out[0] = clampByte(y0);
            out[1] = clampByte((u0 + u1) / 2);
            out[2] = clampByte(y1);
            out[3] = clampByte((v0 + v1) / 2);
            out += 4;
        }
    }
}
#endif

bool isLoopbackDeviceName(const QString &name) {
    return name.contains(QStringLiteral("loopback"), Qt::CaseInsensitive)
        || name.contains(QStringLiteral("Dummy video device"), Qt::CaseInsensitive);
}

} // namespace

struct VirtualCameraProgramSink::Impl {
#ifdef __linux__
    int fd = -1;
    int width = 0;
    int height = 0;
#endif
};

VirtualCameraProgramSink::VirtualCameraProgramSink()
    : m_impl(std::make_unique<Impl>())
{
    m_devicePath = defaultDevicePath();
}

VirtualCameraProgramSink::~VirtualCameraProgramSink() {
    stop();
}

QString VirtualCameraProgramSink::name() const {
    return QStringLiteral("Virtual Camera");
}

bool VirtualCameraProgramSink::isAvailable() const {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool VirtualCameraProgramSink::isActive() const {
    return m_active;
}

void VirtualCameraProgramSink::setDevicePath(const QString &path) {
    if (m_active) return;
    m_devicePath = path;

    QSettings settings;
    settings.beginGroup(QStringLiteral("virtualCamera"));
    settings.setValue(QStringLiteral("devicePath"), m_devicePath);
    settings.endGroup();
}

QStringList VirtualCameraProgramSink::availableLoopbackDevices() {
    QStringList devices;
#ifdef __linux__
    QDir dir(QStringLiteral("/sys/class/video4linux"));
    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        if (!entry.startsWith(QStringLiteral("video")))
            continue;

        QFile nameFile(QStringLiteral("/sys/class/video4linux/") + entry + QStringLiteral("/name"));
        if (!nameFile.open(QIODevice::ReadOnly))
            continue;

        const QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
        if (!isLoopbackDeviceName(name))
            continue;

        const QString devPath = QStringLiteral("/dev/") + entry;
        const int fd = ::open(devPath.toUtf8().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue;

        const bool ok = queryOutputCapability(fd);
        ::close(fd);
        if (ok)
            devices.append(devPath);
    }
#endif
    return devices;
}

QString VirtualCameraProgramSink::defaultDevicePath() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("virtualCamera"));
    const QString saved = settings.value(QStringLiteral("devicePath")).toString();
    settings.endGroup();

    if (!saved.isEmpty())
        return saved;

    const QStringList devices = availableLoopbackDevices();
    if (!devices.isEmpty())
        return devices.first();

    return QStringLiteral("/dev/video42");
}

bool VirtualCameraProgramSink::start(const QString &streamName) {
    Q_UNUSED(streamName);
    stop();

#ifndef __linux__
    return false;
#else
    if (m_devicePath.isEmpty())
        m_devicePath = defaultDevicePath();

    const QByteArray pathUtf8 = m_devicePath.toUtf8();
    m_impl->fd = ::open(pathUtf8.constData(), O_RDWR | O_NONBLOCK);
    if (m_impl->fd < 0) {
        qWarning() << "VirtualCamera: could not open" << m_devicePath
                   << "-" << strerror(errno);
        return false;
    }

    if (!queryOutputCapability(m_impl->fd)) {
        qWarning() << "VirtualCamera:" << m_devicePath << "is not a video output device";
        ::close(m_impl->fd);
        m_impl->fd = -1;
        return false;
    }

    if (!configureOutputFormat(m_impl->fd,
                               VideoWidget::kProgramWidth,
                               VideoWidget::kProgramHeight,
                               m_impl->width,
                               m_impl->height)) {
        qWarning() << "VirtualCamera: could not set YUYV"
                   << VideoWidget::kProgramWidth << "x" << VideoWidget::kProgramHeight
                   << "on" << m_devicePath;
        ::close(m_impl->fd);
        m_impl->fd = -1;
        return false;
    }

    if (!startOutputStream(m_impl->fd)) {
        qWarning() << "VirtualCamera: VIDIOC_STREAMON failed on" << m_devicePath
                   << "-" << strerror(errno);
        ::close(m_impl->fd);
        m_impl->fd = -1;
        return false;
    }

    m_yuyvBuffer.resize(static_cast<qsizetype>(m_impl->width) * m_impl->height * 2);
    m_active = true;

    QSettings settings;
    settings.beginGroup(QStringLiteral("virtualCamera"));
    settings.setValue(QStringLiteral("devicePath"), m_devicePath);
    settings.endGroup();

    qDebug() << "VirtualCamera: streaming program output to" << m_devicePath
             << m_impl->width << "x" << m_impl->height << "YUYV"
             << kFrameRateNum << "fps";
    return true;
#endif
}

void VirtualCameraProgramSink::stop() {
#ifndef __linux__
    m_active = false;
    m_yuyvBuffer.clear();
    return;
#else
    if (m_impl->fd >= 0) {
        stopOutputStream(m_impl->fd);
        ::close(m_impl->fd);
        m_impl->fd = -1;
    }

    m_impl->width  = 0;
    m_impl->height = 0;
    m_active       = false;
    m_yuyvBuffer.clear();
#endif
}

void VirtualCameraProgramSink::submitFrame(const QImage &frame) {
#ifndef __linux__
    Q_UNUSED(frame);
    return;
#else
    if (!m_active || m_impl->fd < 0 || frame.isNull())
        return;

    QImage rgba = frame;
    if (rgba.format() != QImage::Format_RGBA8888)
        rgba = rgba.convertToFormat(QImage::Format_RGBA8888);

    if (rgba.width() != m_impl->width || rgba.height() != m_impl->height) {
        rgba = rgba.scaled(m_impl->width, m_impl->height,
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const qsizetype expected = static_cast<qsizetype>(m_impl->width) * m_impl->height * 2;
    if (m_yuyvBuffer.size() != expected)
        m_yuyvBuffer.resize(expected);

    rgbaToYuyv(rgba.constBits(), m_impl->width, m_impl->height, rgba.bytesPerLine(),
               m_yuyvBuffer.data());

    const ssize_t written = ::write(m_impl->fd, m_yuyvBuffer.constData(),
                                    static_cast<size_t>(m_yuyvBuffer.size()));
    if (written < 0 && errno != EAGAIN)
        qWarning() << "VirtualCamera: write failed -" << strerror(errno);
#endif
}
