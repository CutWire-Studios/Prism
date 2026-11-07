#include "core/effects/Generators.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>

static void initGeneratorResources()
{
    Q_INIT_RESOURCE(resources);
}

namespace {

constexpr const char *kDir = ":/shaders/generators/";

QByteArray readResource(const QString &name) {
    QFile f(QString::fromLatin1(kDir) + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QList<ShaderGeneratorSpec> load() {
    initGeneratorResources();
    QList<ShaderGeneratorSpec> out;
    const QByteArray raw = readResource(QStringLiteral("generators.json"));
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (doc.isNull()) {
        qWarning() << "Generators: generators.json unreadable:" << err.errorString();
        return out;
    }
    for (const QJsonValue &v : doc.object().value("generators").toArray()) {
        const QJsonObject o = v.toObject();
        ShaderGeneratorSpec s;
        s.slug = o.value("slug").toString();
        s.name = o.value("name").toString();
        s.category = o.value("category").toString(QStringLiteral("Generate"));
        s.description = o.value("description").toString();
        s.fragFile = o.value("frag").toString(s.slug + QStringLiteral(".glsl"));
        for (const QJsonValue &pv : o.value("params").toArray())
            s.params << ShaderEffects::parseParam(pv.toObject());
        out << s;
    }
    return out;
}

QByteArray number(double v) {
    return QByteArray::number(v, 'f', 6);
}

QByteArray constantFor(const ShaderParam &p, const QJsonValue &v) {
    switch (p.type) {
    case ShaderParam::Bool:
        return v.toBool() ? "true" : "false";
    case ShaderParam::Int:
    case ShaderParam::Choice:
        return QByteArray::number(qRound(v.toDouble()));
    case ShaderParam::Color: {
        const QJsonArray a = v.toArray();
        return "vec4(" + number(a.at(0).toDouble()) + ", " + number(a.at(1).toDouble()) + ", "
               + number(a.at(2).toDouble()) + ", " + number(a.at(3).toDouble(1.0)) + ")";
    }
    case ShaderParam::Point: {
        const QJsonArray a = v.toArray();
        return "vec2(" + number(a.at(0).toDouble()) + ", " + number(a.at(1).toDouble()) + ")";
    }
    default:
        return number(v.toDouble());
    }
}

// No #version, matching ShaderSource: GLSL 1.10 on desktop compatibility profiles, GLSL ES 1.00 on GLES.
QByteArray assemble(const ShaderGeneratorSpec &spec, const QJsonObject *bake) {
    const QByteArray body = readResource(spec.fragFile);
    if (body.isEmpty())
        return {};

    QByteArray src =
        "#ifdef GL_ES\n"
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
        "precision highp float;\n"
        "#else\n"
        "precision mediump float;\n"
        "#endif\n"
        "precision mediump int;\n"
        "#endif\n"
        "uniform float u_time;\n"
        "uniform vec2 u_resolution;\n"
        "uniform sampler2D u_spectrum;\n"
        "uniform int u_spectrumSize;\n"
        "uniform vec3 u_bands;\n"
        "uniform float u_bass;\n"
        "uniform float u_mid;\n"
        "uniform float u_treble;\n"
        "uniform float u_audioLevel;\n"
        "uniform float u_beat;\n"
        "uniform bool u_hasAudio;\n";
    for (const ShaderParam &p : spec.params) {
        if (p.type == ShaderParam::File)
            continue;
        const QByteArray type = ShaderEffects::glslTypeName(p.type);
        if (bake)
            src += "const " + type + " " + ShaderEffects::uniformName(p) + " = "
                   + constantFor(p, bake->value(p.name)) + ";\n";
        else
            src += "uniform " + type + " " + ShaderEffects::uniformName(p) + ";\n";
    }
    src += ShaderEffects::preludeSource();
    src += "\n";
    src += readResource(QStringLiteral("common.glsl"));
    src += "\n";
    src += body;
    src += "\nvoid main() {\n    gl_FragColor = generate(gl_FragCoord.xy / u_resolution);\n}\n";
    return src;
}

} // namespace

QJsonObject ShaderGeneratorSpec::defaultParams() const {
    return ShaderEffects::defaultParamValues(params);
}

namespace Generators {

const QList<ShaderGeneratorSpec> &all() {
    static const QList<ShaderGeneratorSpec> specs = load();
    return specs;
}

const ShaderGeneratorSpec *bySlug(const QString &slug) {
    for (const ShaderGeneratorSpec &s : all())
        if (s.slug == slug)
            return &s;
    return nullptr;
}

QJsonObject resolveParams(const ShaderGeneratorSpec &spec, const QJsonObject &values) {
    QJsonObject out = spec.defaultParams();
    for (auto it = out.begin(); it != out.end(); ++it)
        if (values.contains(it.key()))
            it.value() = values.value(it.key());
    return out;
}

QByteArray fragmentSource(const ShaderGeneratorSpec &spec) {
    return assemble(spec, nullptr);
}

QByteArray standaloneSource(const ShaderGeneratorSpec &spec, const QJsonObject &values) {
    const QJsonObject resolved = resolveParams(spec, values);
    return assemble(spec, &resolved);
}

} // namespace Generators
