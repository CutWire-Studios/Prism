#include "core/HtmlSource.h"
#include <QWebEngineView>
#include <QTimer>
#include <QPixmap>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>

static constexpr QSize kFrameSize{1280, 720};

HtmlSource::HtmlSource(const QString &html, const QString &filePath) {
    m_view = new QWebEngineView();
    m_view->resize(kFrameSize);
    // Transparent background so HTML/CSS controls what shows through.
    m_view->setAttribute(Qt::WA_TranslucentBackground);
    m_view->page()->setBackgroundColor(Qt::transparent);
    // Keep hidden — WebEngine renders to its internal GPU pipeline regardless.
    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->show();   // must be "shown" to trigger the render pipeline

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
}

void HtmlSource::captureFrame() {
    QPixmap px = m_view->grab();
    if (px.isNull()) return;

    QImage img = px.toImage().scaled(kFrameSize, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGBA8888);
    if (!img.isNull()) {
        m_frame = std::move(img);
        m_dirty = true;
    }
}
