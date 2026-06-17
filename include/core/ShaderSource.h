#pragma once
#include "core/MediaSource.h"
#include <QString>
#include <QSize>
#include <QByteArray>
#include <QElapsedTimer>
#include <memory>

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class AudioAnalyzer;

class ShaderSource : public MediaSource {
public:
    explicit ShaderSource(const QString &fragmentShader,
                          QSize size = QSize(1280, 720));
    ~ShaderSource() override;

    void    setShaderCode(const QString &code);
    QString shaderCode()  const { return m_shaderCode; }
    bool    isCompiled()  const { return m_compiled; }
    QString lastError()   const { return m_lastError; }

    void setAudioSource(const QString &filePath);

    Type           type()      const override { return Type::Shader; }
    bool           isReady()   const override { return m_ready; }
    QSize          frameSize() const override { return m_size; }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_buffer.constData());
    }
    bool    nextFrame()   override;
    QString displayName() const override { return "Shader"; }

private:
    bool initGL();
    void destroyGL();
    bool compileProgram();
    void renderToBuffer();

    QString    m_shaderCode;
    QSize      m_size;
    QByteArray m_buffer;  // RGB24, top-to-bottom

    QOffscreenSurface        *m_surface = nullptr;
    QOpenGLContext           *m_context = nullptr;
    QOpenGLFramebufferObject *m_fbo     = nullptr;
    QOpenGLShaderProgram     *m_program = nullptr;
    unsigned int m_vbo = 0;

    QElapsedTimer m_timer;
    qint64        m_lastFrameMs = 0;
    std::unique_ptr<AudioAnalyzer> m_analyzer;
    unsigned int  m_spectrumTex = 0;

    bool    m_glInitialized = false;
    bool    m_compiled      = false;
    bool    m_ready         = false;
    bool    m_shaderDirty   = false;
    QString m_lastError;
};
