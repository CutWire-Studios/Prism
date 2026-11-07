#include "core/effects/ShaderData.h"

#include "core/media/AudioAnalyzer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QOpenGLShaderProgram>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <algorithm>

namespace ShaderData {

QJsonObject current(const std::shared_ptr<ScriptOutput> &feed) {
    if (!feed)
        return {};
    QString json;
    {
        QMutexLocker lock(&feed->mutex);
        json = feed->json;
    }
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

bool applyUniforms(QOpenGLShaderProgram *program, const QJsonObject &obj) {
    // Generic block: expose every JSON key as u_<key>.
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QByteArray uni = it.key().startsWith(QLatin1String("u_"))
            ? it.key().toUtf8() : ("u_" + it.key().toUtf8());
        const QJsonValue v = it.value();
        if (v.isBool()) {
            program->setUniformValue(uni.constData(), v.toBool());
        } else if (v.isDouble()) {
            program->setUniformValue(uni.constData(), static_cast<GLfloat>(v.toDouble()));
        } else if (v.isArray()) {
            const QJsonArray a = v.toArray();
            if (a.size() == 2)
                program->setUniformValue(uni.constData(),
                    QVector2D(a.at(0).toDouble(), a.at(1).toDouble()));
            else if (a.size() == 3)
                program->setUniformValue(uni.constData(),
                    QVector3D(a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble()));
            else if (a.size() == 4)
                program->setUniformValue(uni.constData(),
                    QVector4D(a.at(0).toDouble(), a.at(1).toDouble(),
                              a.at(2).toDouble(), a.at(3).toDouble()));
            // Larger arrays (e.g. spectrum) are exposed as a texture by the caller.
        } else if (v.isObject()) {
            const QJsonObject o = v.toObject();
            if (o.contains("x") && o.contains("y")) {
                if (o.contains("w"))
                    program->setUniformValue(uni.constData(),
                        QVector4D(o.value("x").toDouble(), o.value("y").toDouble(),
                                  o.value("z").toDouble(), o.value("w").toDouble()));
                else if (o.contains("z"))
                    program->setUniformValue(uni.constData(),
                        QVector3D(o.value("x").toDouble(), o.value("y").toDouble(),
                                  o.value("z").toDouble()));
                else
                    program->setUniformValue(uni.constData(),
                        QVector2D(o.value("x").toDouble(), o.value("y").toDouble()));
            }
        }
    }

    // Audio block: a stable named contract, independent of FFT internals.
    const bool hasAudioFields = obj.contains("low") || obj.contains("mid")
        || obj.contains("high") || obj.contains("level") || obj.contains("beat")
        || obj.contains("spectrum") || obj.contains("hasAudio");
    if (!hasAudioFields)
        return false;
    const float low   = std::clamp(static_cast<float>(obj.value("low").toDouble(0.0)),   0.0f, 1.0f);
    const float mid   = std::clamp(static_cast<float>(obj.value("mid").toDouble(0.0)),   0.0f, 1.0f);
    const float high  = std::clamp(static_cast<float>(obj.value("high").toDouble(0.0)),  0.0f, 1.0f);
    const float level = std::clamp(static_cast<float>(obj.value("level").toDouble(0.0)), 0.0f, 1.0f);
    const float beat  = std::clamp(static_cast<float>(obj.value("beat").toDouble(0.0)),  0.0f, 1.0f);
    const bool hasAudio = obj.value("hasAudio").toBool(level > 0.001f);

    program->setUniformValue("u_bands", QVector3D(low, mid, high));
    program->setUniformValue("u_bass", low);
    program->setUniformValue("u_mid", mid);
    program->setUniformValue("u_treble", high);
    program->setUniformValue("u_audioLevel", level);
    program->setUniformValue("u_beat", beat);
    program->setUniformValue("u_beatPulse", beat);   // legacy uniform name
    program->setUniformValue("u_hasAudio", hasAudio);
    return true;
}

QByteArray spectrumBytes(const QJsonObject &obj) {
    if (!obj.value("spectrum").isArray())
        return {};
    const QJsonArray arr = obj.value("spectrum").toArray();
    const int n = std::min(static_cast<int>(arr.size()), AudioAnalyzer::kMaxBins);
    QByteArray out(std::max(n, 0), 0);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<char>(
            std::clamp(static_cast<float>(arr.at(i).toDouble(0.0)), 0.0f, 1.0f) * 255.0f);
    return out;
}

} // namespace ShaderData
