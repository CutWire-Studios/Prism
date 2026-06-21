#include "ui/canvas/HtmlPreviewWidget.h"
#include "core/sources/HtmlWorkspace.h"
#include <QWebEngineView>

HtmlPreviewWidget::HtmlPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(160, 90);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x18, 0x19, 0x1b));
    setPalette(pal);

    m_webView = new QWebEngineView(this);
    m_webView->resize(HtmlWorkspace::kCanvasWidth, HtmlWorkspace::kCanvasHeight);
    m_webView->setAttribute(Qt::WA_TranslucentBackground);
    m_webView->page()->setBackgroundColor(Qt::transparent);
}

void HtmlPreviewWidget::updateFit() {
    if (!m_webView || width() < 2 || height() < 2)
        return;

    const double scale = std::min(width()  / double(HtmlWorkspace::kCanvasWidth),
                                  height() / double(HtmlWorkspace::kCanvasHeight));
    const int dw = qMax(1, int(HtmlWorkspace::kCanvasWidth  * scale));
    const int dh = qMax(1, int(HtmlWorkspace::kCanvasHeight * scale));

    m_webView->setZoomFactor(scale);
    m_webView->setFixedSize(dw, dh);
    m_webView->move((width() - dw) / 2, (height() - dh) / 2);
}

void HtmlPreviewWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateFit();
}

void HtmlPreviewWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updateFit();
}
