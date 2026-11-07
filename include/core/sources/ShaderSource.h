#pragma once
#include "core/sources/MediaSource.h"
#include "core/effects/ShaderEffects.h"
#include "core/scripting/ScriptOutput.h"
#include <QJsonObject>
#include <QList>
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
struct ShaderGeneratorSpec;

/// Renders a GLSL fragment shader to an offscreen FBO each frame, optionally
/// fed by an AudioAnalyzer for audio-reactive uniforms. Exposes the result as a
/// GPU texture (glTexture()) to avoid a GPU→CPU→GPU round trip.
class ShaderSource : public MediaSource {
public:
    explicit ShaderSource(const QString &fragmentShader,
                          QSize size = QSize(1280, 720));
    ~ShaderSource() override;

    void    setShaderCode(const QString &code);
    QString shaderCode()  const { return m_shaderCode; }
    bool    isCompiled()  const { return m_compiled; }
    QString lastError()   const { return m_lastError; }

    /// Switch to a generator preset: assembles its shader and declares its params as uniforms,
    /// starting at the defaults.
    void setPreset(const ShaderGeneratorSpec &spec);
    /// Param values for a preset, applied as uniforms on the next frame without recompiling.
    bool updateParams(const QJsonObject &params) override;
    /// Number of times the shader program has been compiled.
    int  compileCount() const { return m_compileCount; }

    void setAudioSource(const QString &filePath);
    void setAudioSyncState(double playbackTime, bool playing, double speed = 1.0);
    void clearAudioSyncState();
    void setDataSource(std::shared_ptr<ScriptOutput> data);

    Type           type()      const override { return Type::Shader; }
    bool           isReady()   const override { return m_ready; }
    QSize          frameSize() const override { return m_size; }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_buffer.constData());
    }
    bool    nextFrame()   override;
    QString displayName() const override { return "Generator"; }

private:
    bool initGL();
    void destroyGL();
    bool compileProgram();
    void renderToBuffer();
    void applyParamUniforms();
    /// Map an arbitrary JSON object to shader uniforms (generic key→u_key plus the
    /// stable named audio block). Sets @p hasAudioBlock when audio fields present.
    void applyDataUniforms(const class QJsonObject &obj, bool &hasAudioBlock);

    QString    m_shaderCode;
    QList<ShaderParam> m_paramSpecs;
    QJsonObject        m_params;
    int        m_compileCount = 0;
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
    std::shared_ptr<ScriptOutput> m_dataSource;
    uint          m_dataVersion = 0;
    QString       m_audioSourcePath;
    unsigned int  m_spectrumTex = 0;
    int           m_spectrumTexWidth = 0;   // current uploaded spectrum length
    bool          m_audioSyncEnabled = false;
    bool          m_audioPlaying = true;
    double        m_audioSyncTime = 0.0;
    double        m_audioSyncSpeed = 1.0;

    bool    m_glInitialized = false;
    bool    m_compiled      = false;
    bool    m_ready         = false;
    bool    m_shaderDirty   = false;
    QString m_lastError;
};
