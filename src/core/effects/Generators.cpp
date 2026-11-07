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

QJsonValue defaultFor(const ShaderParam &p, const QJsonValue &v) {
    switch (p.type) {
    case ShaderParam::Color: {
        const QJsonArray a = v.toArray();
        auto hex = [&](int i) { return QStringLiteral("%1").arg(qRound(a.at(i).toDouble(i == 3 ? 1.0 : 0.0) * 255), 2, 16, QLatin1Char('0')); };
        return QStringLiteral("#") + hex(0) + hex(1) + hex(2) + hex(3);
    }
    default:
        return v;
    }
}

QByteArray paramsHeader(const ShaderGeneratorSpec &spec, const QJsonObject &values) {
    static const char *const kTypes[] = {"float", "int", "bool", "choice", "color", "point", "file"};
    QJsonArray arr;
    for (const ShaderParam &p : spec.params) {
        if (p.type == ShaderParam::File)
            continue;
        QJsonObject o{{"name", p.name}, {"type", QString::fromLatin1(kTypes[p.type])}, {"label", p.label},
                      {"default", defaultFor(p, values.value(p.name))}};
        if (p.type == ShaderParam::Float || p.type == ShaderParam::Int) {
            o["min"] = p.min;
            o["max"] = p.max;
            o["step"] = p.step;
        }
        if (p.type == ShaderParam::Choice)
            o["choices"] = QJsonArray::fromStringList(p.choices);
        arr.append(o);
    }
    return "/* @params\n" + QJsonDocument(arr).toJson(QJsonDocument::Indented) + "*/\n";
}

// No #version, matching ShaderSource: GLSL 1.10 on desktop compatibility profiles, GLSL ES 1.00 on GLES.
QByteArray assemble(const ShaderGeneratorSpec &spec) {
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
    return assemble(spec);
}

QByteArray editableSource(const ShaderGeneratorSpec &spec, const QJsonObject &values) {
    const QByteArray code = assemble(spec);
    return code.isEmpty() ? code : paramsHeader(spec, resolveParams(spec, values)) + code;
}

} // namespace Generators
