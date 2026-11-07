#pragma once

#include "core/scripting/ScriptOutput.h"

#include <QByteArray>
#include <QJsonObject>
#include <memory>

class QOpenGLShaderProgram;

/// The JSON-to-uniform mapping shared by every shader that takes a data feed (see
/// docs/shader-data-interface.md). The program must be bound.
namespace ShaderData {

/// The feed's current JSON object; empty when there is no feed or it is not an object.
QJsonObject current(const std::shared_ptr<ScriptOutput> &feed);

/// Sets u_<key> for every key plus the named audio block. True when the object carried audio fields.
bool applyUniforms(QOpenGLShaderProgram *p, const QJsonObject &obj);

/// The "spectrum" array as 0..255 bytes (at most AudioAnalyzer::kMaxBins); empty when absent.
QByteArray spectrumBytes(const QJsonObject &obj);

} // namespace ShaderData
