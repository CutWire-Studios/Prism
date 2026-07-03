#include "ui/output/NdiProgramSink.h"
#include "ui/canvas/VideoWidget.h"
#include "core/sources/NdiLibrary.h"

#ifdef PRISM_HAVE_NDI
#include <cstddef>
#include <Processing.NDI.Lib.h>
#endif

struct NdiProgramSink::Impl {
#ifdef PRISM_HAVE_NDI
    NDIlib_send_instance_t sender = nullptr;
#endif
};

NdiProgramSink::NdiProgramSink()
    : m_impl(std::make_unique<Impl>())
{
}

NdiProgramSink::~NdiProgramSink() {
    stopInternal();
}

QString NdiProgramSink::name() const {
    return QStringLiteral("NDI");
}

bool NdiProgramSink::isAvailable() const {
    return NdiLibrary::instance().isAvailable();
}

bool NdiProgramSink::isActive() const {
    return m_active;
}

bool NdiProgramSink::start(const QString &streamName) {
    stop();

#ifndef PRISM_HAVE_NDI
    Q_UNUSED(streamName);
    return false;
#else
    if (!NdiLibrary::instance().acquire())
        return false;

    m_ndiName = streamName.isEmpty()
              ? QStringLiteral("CutWire Prism Program")
              : streamName;

    NDIlib_send_create_t desc{};
    const QByteArray ndiNameUtf8 = m_ndiName.toUtf8();
    desc.p_ndi_name = ndiNameUtf8.constData();
    desc.clock_video = true;
    desc.clock_audio = false;

    m_impl->sender = NDIlib_send_create(&desc);
    if (!m_impl->sender) {
        NdiLibrary::instance().release();
        return false;
    }

    m_active = true;
    return true;
#endif
}

void NdiProgramSink::stop() {
    stopInternal();
}

void NdiProgramSink::stopInternal() {
#ifndef PRISM_HAVE_NDI
    m_active = false;
    m_ndiName.clear();
    m_frameBuffer = {};
    return;
#else
    if (m_impl->sender) {
        NDIlib_send_send_video_v2(m_impl->sender, nullptr);
        NDIlib_send_destroy(m_impl->sender);
        m_impl->sender = nullptr;
    }
    if (m_active)
        NdiLibrary::instance().release();

    m_active = false;
    m_ndiName.clear();
    m_frameBuffer = {};
#endif
}

void NdiProgramSink::submitFrame(const QImage &frame) {
#ifndef PRISM_HAVE_NDI
    Q_UNUSED(frame);
    return;
#else
    if (!m_active || !m_impl->sender || frame.isNull())
        return;

    QImage rgba = frame;
    if (rgba.format() != QImage::Format_RGBA8888)
        rgba = rgba.convertToFormat(QImage::Format_RGBA8888);

    if (rgba.width() != VideoWidget::kProgramWidth ||
        rgba.height() != VideoWidget::kProgramHeight)
        rgba = rgba.scaled(VideoWidget::kProgramWidth, VideoWidget::kProgramHeight,
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    m_frameBuffer = rgba;

    NDIlib_video_frame_v2_t video{};
    video.xres                   = m_frameBuffer.width();
    video.yres                   = m_frameBuffer.height();
    video.FourCC                 = NDIlib_FourCC_type_RGBA;
    video.frame_rate_N           = 30;
    video.frame_rate_D           = 1;
    video.picture_aspect_ratio   = static_cast<float>(video.xres) / static_cast<float>(video.yres);
    video.frame_format_type      = NDIlib_frame_format_type_progressive;
    video.timecode               = NDIlib_send_timecode_synthesize;
    video.p_data                 = m_frameBuffer.bits();
    video.line_stride_in_bytes   = m_frameBuffer.bytesPerLine();
    video.p_metadata             = nullptr;

    NDIlib_send_send_video_v2(m_impl->sender, &video);
#endif
}
