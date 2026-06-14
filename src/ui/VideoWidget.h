#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>
#include <memory>
#include "../core/VideoPlayer.h"

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

    // ── Dual-player A/B API ───────────────────────────────────────────────
    void loadVideoA(const QString &filePath);
    void loadVideoB(const QString &filePath);

    void playA();
    void pauseA();
    void playB();
    void pauseB();

    void seekA(double seconds);
    void seekB(double seconds);

    void setRepeatA(bool repeat) { m_repeatA = repeat; }
    void setRepeatB(bool repeat) { m_repeatB = repeat; }
    void setTrimPointsA(double startSec, double endSec);
    void setTrimPointsB(double startSec, double endSec);

    bool isPlayingA() const { return m_playingA; }
    bool isPlayingB() const { return m_playingB; }
    double getCurrentTimeA() const;
    double getDurationA() const;
    double getCurrentTimeB() const;
    double getDurationB() const;

    // Switch which player is rendered on-screen (no restart)
    void setShowA(bool showA);

    // Current frame as QImage for preview labels
    QImage getFrameA() const;
    QImage getFrameB() const;

    // ── Backward-compat single-player API (delegates to player A) ─────────
    void loadVideo(const QString &path)          { loadVideoA(path); }
    void play()                                  { playA(); }
    void pause()                                 { pauseA(); }
    void stop();
    void seek(double s)                          { seekA(s); }
    void setRepeat(bool r)                       { setRepeatA(r); }
    void setTrimPoints(double s, double e)       { setTrimPointsA(s, e); }
    bool isPlaying() const                       { return m_playingA; }
    double getCurrentTime() const                { return getCurrentTimeA(); }
    double getDuration() const                   { return getDurationA(); }

    QSize sizeHint() const override { return QSize(1280, 720); }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void updateFrame();

private:
    std::unique_ptr<VideoPlayer> m_playerA;
    std::unique_ptr<VideoPlayer> m_playerB;
    GLuint m_textureA = 0;
    GLuint m_textureB = 0;
    bool m_playingA = false;
    bool m_playingB = false;
    bool m_showA = true;
    bool m_repeatA = false;
    bool m_repeatB = false;
    double m_trimStartA = 0.0;
    double m_trimEndA = -1.0;
    double m_trimStartB = 0.0;
    double m_trimEndB = -1.0;
    QTimer *m_frameTimer = nullptr;

    void setupTextureGL(GLuint &tex, VideoPlayer *player);
    void uploadFrameGL(GLuint tex, VideoPlayer *player);
    void renderTexture(GLuint tex);
    bool advancePlayer(VideoPlayer *player, bool &playing, bool repeat,
                       double trimStart, double trimEnd);
};
