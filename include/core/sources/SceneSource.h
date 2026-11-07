#pragma once

#include "core/sources/MediaSource.h"

#include <QElapsedTimer>
#include <QImage>
#include <QString>
#include <memory>

/// Another scene's final composite as a video-only source. The wrapped source is the target scene's
/// graph evaluated into a MediaSource tree (built by the app through SourceFactory), so downstream
/// process effects treat it like any other input. With no wrapped source (scene deleted, nesting
/// too deep) it presents a transparent frame.
///
/// The wrapped tree has no deck driving it, so this source paces it: a timeline source plays at its
/// native rate and loops, anything else advances one frame per call.
class SceneSource : public MediaSource {
public:
    SceneSource(std::unique_ptr<MediaSource> content, QString name);

    // Reports the content type like the decorators do, so the compositor paces it the same way.
    Type    type()        const override { return m_content ? m_content->type() : Type::Canvas; }
    bool    isReady()     const override { return m_content ? m_content->isReady() : true; }
    QSize   frameSize()   const override { return m_content ? m_content->frameSize() : m_empty.size(); }
    const uint8_t *frameData() const override { return m_content ? m_content->frameData() : m_empty.constBits(); }
    int     frameBytesPerLine() const override { return m_content ? m_content->frameBytesPerLine() : m_empty.bytesPerLine(); }
    bool    hasAlpha()    const override { return m_content ? m_content->hasAlpha() : true; }
    unsigned int glTexture() const override { return m_content ? m_content->glTexture() : 0; }
    bool    nextFrame()         override;
    bool    presentAt(double)   override { return nextFrame(); }
    QString displayName() const override { return m_name; }
    void    setOnAir(bool on)   override { if (m_content) m_content->setOnAir(on); }
    double  requestOut()        override { return m_content ? m_content->requestOut() : 0.0; }

    bool hasContent() const { return m_content != nullptr; }

private:
    std::unique_ptr<MediaSource> m_content;
    QString       m_name;
    QImage        m_empty;
    QElapsedTimer m_clock;
    double        m_anchor = 0.0;
};
