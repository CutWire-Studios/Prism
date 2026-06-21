#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>

/// Displays a cached program frame (uploaded each tick from the compositor).
class ProgramMirrorWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit ProgramMirrorWidget(QWidget *parent = nullptr);
    ~ProgramMirrorWidget() override;

    void setFrame(const QImage &frame);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void uploadFrameGL();

    QImage m_frame;
    GLuint m_displayTex = 0;
    int    m_texW       = 0;
    int    m_texH       = 0;
};
