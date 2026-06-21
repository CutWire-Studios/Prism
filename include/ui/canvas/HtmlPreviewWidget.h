#pragma once

#include <QWidget>

class QWebEngineView;

// Renders HTML at 1280×720 internally and scales to fit the panel (no scrollbars).
class HtmlPreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit HtmlPreviewWidget(QWidget *parent = nullptr);

    QWebEngineView *webView() const { return m_webView; }

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateFit();

    QWebEngineView *m_webView = nullptr;
};
