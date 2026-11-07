#include "core/effects/ShaderEffects.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>

// Static-library resources are only pulled into the link when something references them.
static void initShaderEffectResources()
{
    Q_INIT_RESOURCE(resources);
}

namespace {

constexpr const char *kDir = ":/shaders/effects/";

ShaderParam::Type typeFromString(const QString &s) {
    if (s == QLatin1String("int"))    return ShaderParam::Int;
    if (s == QLatin1String("bool"))   return ShaderParam::Bool;
    if (s == QLatin1String("choice")) return ShaderParam::Choice;
    if (s == QLatin1String("color"))  return ShaderParam::Color;
    if (s == QLatin1String("point"))  return ShaderParam::Point;
    return ShaderParam::Float;
}

ShaderParam parseParam(const QJsonObject &o) {
    ShaderParam p;
    p.name = o.value("name").toString();
    p.type = typeFromString(o.value("type").toString());
    p.label = o.value("label").toString(p.name);
    p.min = o.value("min").toDouble(0.0);
    p.max = o.value("max").toDouble(1.0);
    p.step = o.value("step").toDouble(p.type == ShaderParam::Float ? 0.01 : 1.0);
    for (const QJsonValue &v : o.value("choices").toArray())
        p.choices << v.toString();
    if (p.type == ShaderParam::Choice) {
        p.min = 0;
        p.max = qMax(0, p.choices.size() - 1);
    }
    const QJsonValue d = o.value("default");
    if (p.type == ShaderParam::Color) {
        p.defColor = ShaderEffects::parseHexColor(d.toString(QStringLiteral("#ffffffff")));
    } else if (p.type == ShaderParam::Point) {
        const QJsonArray a = d.toArray();
        if (a.size() == 2)
            p.defPoint = QPointF(a.at(0).toDouble(), a.at(1).toDouble());
    } else if (d.isBool()) {
        p.def = d.toBool() ? 1.0 : 0.0;
    } else {
        p.def = d.toDouble(0.0);
    }
    return p;
}

QList<ShaderEffectSpec> load() {
    initShaderEffectResources();
    QList<ShaderEffectSpec> out;
    QFile f(QString::fromLatin1(kDir) + QStringLiteral("effects.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "ShaderEffects: effects.json missing";
        return out;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull()) {
        qWarning() << "ShaderEffects: effects.json parse error:" << err.errorString();
        return out;
    }
    for (const QJsonValue &v : doc.object().value("effects").toArray()) {
        const QJsonObject o = v.toObject();
        ShaderEffectSpec s;
        s.id = o.value("id").toInt(-1);
        s.slug = o.value("slug").toString();
        s.name = o.value("name").toString();
        s.category = o.value("category").toString();
        s.description = o.value("description").toString();
        s.fragFile = o.value("frag").toString(s.slug + QStringLiteral(".frag"));
        s.multiPass = o.value("multiPass").toBool(false);
        s.needsPrevFrame = o.value("needsPrevFrame").toBool(false);
        s.extraInputs = o.value("extraInputs").toInt(0);
        for (const QJsonValue &pv : o.value("params").toArray())
            s.params << parseParam(pv.toObject());
        out << s;
    }
    return out;
}

QByteArray readResource(const QString &name) {
    QFile f(QString::fromLatin1(kDir) + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const char *glslType(ShaderParam::Type t) {
    switch (t) {
    case ShaderParam::Int:
    case ShaderParam::Choice: return "int";
    case ShaderParam::Bool:   return "bool";
    case ShaderParam::Color:  return "vec4";
    case ShaderParam::Point:  return "vec2";
    default:                  return "float";
    }
}

} // namespace

QJsonObject ShaderEffectSpec::defaultParams() const {
    QJsonObject o;
    for (const ShaderParam &p : params) {
        switch (p.type) {
        case ShaderParam::Bool:   o[p.name] = p.def != 0.0; break;
        case ShaderParam::Int:
        case ShaderParam::Choice: o[p.name] = int(p.def); break;
        case ShaderParam::Color:
            o[p.name] = QJsonArray{p.defColor.redF(), p.defColor.greenF(),
                                   p.defColor.blueF(), p.defColor.alphaF()};
            break;
        case ShaderParam::Point:  o[p.name] = QJsonArray{p.defPoint.x(), p.defPoint.y()}; break;
        default:                  o[p.name] = p.def; break;
        }
    }
    return o;
}

namespace ShaderEffects {

const QList<ShaderEffectSpec> &all() {
    static const QList<ShaderEffectSpec> specs = load();
    return specs;
}

const ShaderEffectSpec *bySlug(const QString &slug) {
    for (const ShaderEffectSpec &s : all())
        if (s.slug == slug) return &s;
    return nullptr;
}

const ShaderEffectSpec *byId(int id) {
    for (const ShaderEffectSpec &s : all())
        if (s.id == id) return &s;
    return nullptr;
}

QByteArray uniformName(const ShaderParam &p) {
    return "p_" + p.name.toUtf8();
}

QByteArray fragmentSource(const ShaderEffectSpec &spec) {
    const QByteArray body = readResource(spec.fragFile);
    if (body.isEmpty()) return {};

    // No #version: matches the other GL shaders (GLSL 1.10 compat / GLSL ES 1.00 via
    // attribute/varying/texture2D/gl_FragColor), so the same text builds on desktop and GLES.
    QByteArray src =
        "#ifdef GL_ES\n"
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
        "precision highp float;\n"
        "#else\n"
        "precision mediump float;\n"
        "#endif\n"
        "precision mediump int;\n"
        "#endif\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_input;\n"
        "uniform vec2 u_texel;\n"
        "uniform float u_time;\n"
        "uniform vec2 u_resolution;\n"
        "uniform vec3 u_bands;\n"
        "uniform float u_bass;\n"
        "uniform float u_mid;\n"
        "uniform float u_treble;\n"
        "uniform float u_audioLevel;\n"
        "uniform float u_beat;\n"
        "uniform bool u_hasAudio;\n";
    for (const ShaderParam &p : spec.params)
        src += QByteArray("uniform ") + glslType(p.type) + " " + uniformName(p) + ";\n";
    src += readResource(QStringLiteral("prelude.glsl"));
    src += "\n";
    src += body;
    src += "\nvoid main() {\n    gl_FragColor = effect(v_uv);\n}\n";
    return src;
}

QColor parseHexColor(const QString &s) {
    if (s.size() != 9 || !s.startsWith(u'#')) return QColor(s);
    bool ok = false;
    const quint32 v = s.mid(1).toUInt(&ok, 16);
    if (!ok) return QColor(s);
    return QColor(int(v >> 24), int((v >> 16) & 0xff), int((v >> 8) & 0xff), int(v & 0xff));
}

} // namespace ShaderEffects
