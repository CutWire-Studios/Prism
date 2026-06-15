#pragma once

#include "core/MediaSource.h"
#include <QObject>
#include <QImage>
#include <QString>

class QWebEngineView;
class QTimer;

// MediaSource that renders an HTML/CSS/JS page (including Tailwind CSS) via
// Qt WebEngine and captures frames at ~30 fps.
//
// The view is kept hidden at 1280×720. After the page finishes loading,
// a timer calls grab() every 33 ms, converts the result to RGB888, and
// marks the frame dirty so VideoWidget picks it up on the next tick.
//
// Usage:
//   auto src = std::make_unique<HtmlSource>(htmlString);
//   // OR for a local file:
//   auto src = std::make_unique<HtmlSource>("", "/path/to/overlay.html");
//   videoWidget->setSourceA(std::move(src));
//   videoWidget->playA();

class HtmlSource : public QObject, public MediaSource {
    Q_OBJECT

public:
    // Pass inline HTML, or leave html empty and provide a filePath.
    explicit HtmlSource(const QString &html, const QString &filePath = {});
    ~HtmlSource() override;

    Type    type()        const override { return Type::Html; }
    bool    isReady()     const override { return m_ready; }
    QSize   frameSize()   const override { return {1280, 720}; }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_frame.constBits());
    }
    bool    nextFrame()         override;
    QString displayName() const override { return m_name; }
    bool    hasAlpha()    const override { return true; } // always RGBA

private slots:
    void onLoadFinished(bool ok);
    void captureFrame();

private:
    QWebEngineView *m_view    = nullptr;
    QTimer         *m_timer   = nullptr;

    QImage  m_frame;
    QString m_name;
    bool    m_ready = false;
    bool    m_dirty = false;
};
