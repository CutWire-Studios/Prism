#pragma once

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

struct ShaderParam {
    enum Type { Float, Int, Bool, Choice, Color, Point, File };

    QString name;
    Type    type = Float;
    double  min = 0.0, max = 1.0, def = 0.0, step = 0.01;
    QStringList choices;
    QString label;
    QColor  defColor = Qt::white;
    QPointF defPoint = QPointF(0.5, 0.5);
    QString fileFilter;   // File: dialog filter, e.g. "*.cube *.png"
};

struct ShaderEffectSpec {
    int     id = -1;
    QString slug, name, category, description;
    QList<ShaderParam> params;
    QString fragFile;
    bool    multiPass = false;
    bool    needsPrevFrame = false;   // binds the previous output as u_prev
    int     historyFrames = 0;        // > 0 binds the last N input frames as u_history (see historyFrame())
    int     extraInputs = 0;
    QStringList inputNames;   // names of the extra input ports; defaults to in1..inN

    /// Name of extra input @p i (0-based).
    QString inputName(int i) const;

    /// Default node params keyed by param name (Color -> [r,g,b,a] 0..1, Point -> [x,y], File -> path string).
    QJsonObject defaultParams() const;
};

/// Data-driven fragment-shader effects declared in :/shaders/effects/effects.json.
namespace ShaderEffects {

const QList<ShaderEffectSpec> &all();
const ShaderEffectSpec *bySlug(const QString &slug);
const ShaderEffectSpec *byId(int id);

/// Full GLSL for @p spec: header, uniforms, prelude, effect body and main(). Empty if the
/// body resource is missing.
QByteArray fragmentSource(const ShaderEffectSpec &spec);

/// Parses one entry of a "params" array (shared with the generator manifest).
ShaderParam parseParam(const QJsonObject &o);

/// What the first `/* @params ... */` comment of a shader declares: either a JSON array of params
/// (entries as in effects.json) or an object `{ "params": [...], "history": N }`.
struct ShaderHeader {
    QList<ShaderParam> params;
    int history = 0;   // frames of u_history, 0 when the header does not say
};

/// Empty header if there is none; on malformed JSON sets @p error and returns an empty header.
ShaderHeader parseHeader(const QString &code, QString *error = nullptr);

/// The params of parseHeader.
QList<ShaderParam> parseHeaderParams(const QString &code, QString *error = nullptr);

/// Spec for a user-written process-node shader: params and history from the header, extra inputs
/// from the highest `uniform sampler2D u_input<N>` it declares, u_prev / u_history when declared.
/// Not a registered effect (id -1, no fragFile).
ShaderEffectSpec customSpec(const QString &code, QString *error = nullptr);

/// Starting code of a new Shader process node.
QString defaultCustomCode();

/// Default values keyed by param name, in the shape ShaderEffectSpec::defaultParams returns.
QJsonObject defaultParamValues(const QList<ShaderParam> &params);

/// GLSL type of the uniform that carries a param of type @p t.
const char *glslTypeName(ShaderParam::Type t);

/// Contents of prelude.glsl.
QByteArray preludeSource();

/// Name of the uniform that carries @p p.
QByteArray uniformName(const ShaderParam &p);

/// Parses "#rrggbb" or "#rrggbbaa" (CSS order, alpha last); anything else goes to QColor(string).
QColor parseHexColor(const QString &s);

} // namespace ShaderEffects
