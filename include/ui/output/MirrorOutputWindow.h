#pragma once

#include <QMainWindow>
#include <QImage>
#include <QRect>

class ProgramMirrorWidget;

/// Secondary output window that mirrors the program compositor feed.
class MirrorOutputWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MirrorOutputWindow(QWidget *parent = nullptr);

    ProgramMirrorWidget *mirrorWidget() const { return m_mirror; }

    void setFrame(const QImage &frame);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onFullscreenClicked();

private:
    void enterFullscreen();
    void exitFullscreen();
    void updateFullscreenIcon();
    bool isFullscreenActive() const;

    ProgramMirrorWidget *m_mirror = nullptr;
    class QPushButton   *m_fullscreenBtn = nullptr;
    // See OutputWindow: macOS fullscreen is geometry-based, so track it here too.
    bool  m_fullscreen = false;
    QRect m_normalGeometry;
};
