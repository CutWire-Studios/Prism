#include "core/sources/HtmlSource.h"
#include <QWebEngineView>
#include <QTimer>
#include <QPixmap>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>

static constexpr QSize kFrameSize{1280, 720};

HtmlSource::HtmlSource(const QString &html, const QString &filePath) {
    m_frame = QImage(kFrameSize, QImage::Format_RGBA8888);
    m_frame.fill(Qt::transparent);

    m_view = new QWebEngineView();
    m_view->resize(kFrameSize);
    m_view->setFixedSize(kFrameSize);
    // Do not use WA_TranslucentBackground — it causes grab() ghost trails on
    // Linux/Wayland when combined with animated / glowing HTML content.
    m_view->page()->setBackgroundColor(Qt::transparent);
    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->show(); // must be shown for the Chromium render pipeline

    connect(m_view, &QWebEngineView::loadFinished,
            this,   &HtmlSource::onLoadFinished);

    if (!filePath.isEmpty()) {
        m_name = QFileInfo(filePath).fileName();
        m_view->load(QUrl::fromLocalFile(filePath));
    } else {
        m_name = "HTML Overlay";
        m_view->setHtml(html, QUrl("qrc:/"));
    }

    m_timer = new QTimer(this);
    m_timer->setInterval(33); // ~30 fps
    connect(m_timer, &QTimer::timeout, this, &HtmlSource::captureFrame);
}

HtmlSource::~HtmlSource() {
    m_timer->stop();
    delete m_view;
}

bool HtmlSource::nextFrame() {
    if (!m_dirty) return false;
    m_dirty = false;
    return true;
}

void HtmlSource::onLoadFinished(bool ok) {
    if (!ok) {
        qWarning() << "HtmlSource: page load failed";
        return;
    }
    qDebug() << "HtmlSource: page loaded —" << m_name;
    m_ready = true;
    m_timer->start();
    // First paint after load — grab() needs a tick before pixels are ready.
    QTimer::singleShot(100, this, &HtmlSource::captureFrame);
}

void HtmlSource::captureFrame() {
    if (!m_view || !m_ready)
        return;

    const QPixmap px = m_view->grab(QRect(QPoint(0, 0), kFrameSize));
    if (px.isNull())
        return;

    QImage img = px.toImage().convertToFormat(QImage::Format_RGBA8888);
    if (img.size() != kFrameSize) {
        img = img.scaled(kFrameSize, Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
    }
    if (img.isNull())
        return;

    m_frame = std::move(img);
    m_dirty = true;
}
