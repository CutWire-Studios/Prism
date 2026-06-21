#include "core/sources/NdiSource.h"
#include "core/sources/NdiLibrary.h"

#ifdef SWITCHX_HAVE_NDI
#include <cstddef>
#include <Processing.NDI.Lib.h>
#endif

NdiSource::NdiSource() = default;

NdiSource::~NdiSource() {
    disconnect();
}

bool NdiSource::isAvailable() {
    return NdiLibrary::instance().isAvailable();
}

QStringList NdiSource::discoverSources(int waitMs) {
#ifndef SWITCHX_HAVE_NDI
    Q_UNUSED(waitMs);
    return {};
#else
    if (!NdiLibrary::instance().acquire())
        return {};

    NDIlib_find_create_t findCreate{};
    NDIlib_find_instance_t finder = NDIlib_find_create_v2(&findCreate);

    QStringList names;
    if (finder) {
        if (waitMs > 0)
            NDIlib_find_wait_for_sources(finder, static_cast<uint32_t>(waitMs));

        uint32_t count = 0;
        const NDIlib_source_t *sources = NDIlib_find_get_current_sources(finder, &count);
        for (uint32_t i = 0; i < count; ++i) {
            if (sources[i].p_ndi_name)
                names << QString::fromUtf8(sources[i].p_ndi_name);
        }
        NDIlib_find_destroy(finder);
    }

    NdiLibrary::instance().release();
    return names;
#endif
}

bool NdiSource::connectTo(const QString &ndiName) {
    disconnect();
    if (ndiName.isEmpty()) return false;

#ifndef SWITCHX_HAVE_NDI
    Q_UNUSED(ndiName);
    return false;
#else
    if (!NdiLibrary::instance().acquire())
        return false;

    m_ndiName = ndiName;

    NDIlib_source_t source{};
    const QByteArray nameUtf8 = m_ndiName.toUtf8();
    source.p_ndi_name = nameUtf8.constData();

    NDIlib_recv_create_v3_t recvDesc{};
    recvDesc.source_to_connect_to = source;
    recvDesc.color_format           = NDIlib_recv_color_format_RGBX_RGBA;
    recvDesc.bandwidth              = NDIlib_recv_bandwidth_highest;

    m_recv = NDIlib_recv_create_v3(&recvDesc);
    if (!m_recv) {
        NdiLibrary::instance().release();
        m_ndiName.clear();
        return false;
    }

    m_connected = true;
    m_name = ndiName;
    return true;
#endif
}

void NdiSource::disconnect() {
#ifdef SWITCHX_HAVE_NDI
    if (m_recv) {
        NDIlib_recv_destroy(m_recv);
        m_recv = nullptr;
    }
#endif
    if (m_connected) {
        NdiLibrary::instance().release();
        m_connected = false;
    }
    m_frame   = {};
    m_dirty   = false;
    m_ndiName = {};
}

void NdiSource::storeVideoFrame(const QImage &img) {
    if (img.isNull()) return;
    m_frame = img.convertToFormat(QImage::Format_RGB888);
    m_dirty = true;
}

bool NdiSource::nextFrame() {
#ifndef SWITCHX_HAVE_NDI
    return false;
#else
    if (!m_recv) return false;

    NDIlib_video_frame_v2_t video{};
    NDIlib_audio_frame_v3_t audio{};
    NDIlib_metadata_frame_t meta{};

    const NDIlib_frame_type_e frameType =
        NDIlib_recv_capture_v3(m_recv, &video, &audio, &meta, 0);

    if (audio.p_data)
        NDIlib_recv_free_audio_v3(m_recv, &audio);
    if (meta.p_data)
        NDIlib_recv_free_metadata(m_recv, &meta);

    if (frameType != NDIlib_frame_type_video) {
        if (!m_dirty) return false;
        m_dirty = false;
        return true;
    }

    if (!video.p_data || video.xres <= 0 || video.yres <= 0) {
        NDIlib_recv_free_video_v2(m_recv, &video);
        return false;
    }

    QImage img;
    const int stride = video.line_stride_in_bytes > 0
                     ? video.line_stride_in_bytes
                     : video.xres * 4;

    switch (video.FourCC) {
    case NDIlib_FourCC_type_RGBA:
        img = QImage(video.p_data, video.xres, video.yres, stride, QImage::Format_RGBA8888).copy();
        break;
    case NDIlib_FourCC_type_RGBX:
        img = QImage(video.p_data, video.xres, video.yres, stride, QImage::Format_RGBX8888).copy();
        break;
    case NDIlib_FourCC_type_BGRA:
        img = QImage(video.p_data, video.xres, video.yres, stride, QImage::Format_ARGB32).copy();
        img = img.convertToFormat(QImage::Format_RGB888);
        break;
    case NDIlib_FourCC_type_BGRX:
        img = QImage(video.p_data, video.xres, video.yres, stride, QImage::Format_RGB32).copy();
        break;
    default:
        NDIlib_recv_free_video_v2(m_recv, &video);
        return false;
    }

    NDIlib_recv_free_video_v2(m_recv, &video);

    if (!img.isNull())
        storeVideoFrame(img);

    if (!m_dirty) return false;
    m_dirty = false;
    return true;
#endif
}
