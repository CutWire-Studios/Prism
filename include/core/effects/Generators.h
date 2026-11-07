#pragma once

#include "core/effects/ShaderEffects.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

/// A shader preset for a Shader input source, declared in :/shaders/generators/generators.json.
/// Its body implements `vec4 generate(vec2 uv)`; uv is 0..1 with the origin at the bottom left.
struct ShaderGeneratorSpec {
    QString slug, name, category, description;
    QList<ShaderParam> params;
    QString fragFile;

    QJsonObject defaultParams() const;
};

namespace Generators {

const QList<ShaderGeneratorSpec> &all();
const ShaderGeneratorSpec *bySlug(const QString &slug);

/// Full GLSL for @p spec with every param as a `p_<name>` uniform. Empty if the body is missing.
QByteArray fragmentSource(const ShaderGeneratorSpec &spec);

/// Same program with the param values baked in as constants, for editing as a custom shader.
/// Missing values fall back to the defaults.
QByteArray standaloneSource(const ShaderGeneratorSpec &spec, const QJsonObject &values);

/// @p values over the spec's defaults, limited to the spec's params.
QJsonObject resolveParams(const ShaderGeneratorSpec &spec, const QJsonObject &values);

} // namespace Generators
