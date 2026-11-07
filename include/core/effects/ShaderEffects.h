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
    bool    needsPrevFrame = false;
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

/// Name of the uniform that carries @p p.
QByteArray uniformName(const ShaderParam &p);

/// Parses "#rrggbb" or "#rrggbbaa" (CSS order, alpha last); anything else goes to QColor(string).
QColor parseHexColor(const QString &s);

} // namespace ShaderEffects
