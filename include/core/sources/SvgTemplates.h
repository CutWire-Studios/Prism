#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace prism {

// SVG templates carry their parameters in a <metadata id="prism"> element holding JSON:
//   {"name": "Clock", "params": [{"name": "color", "label": "Time color", "type": "color",
//                                 "default": "#00e5ff"}, ...]}
// type is "text", "color" or "number". Tokens like {name} in text content and attribute values
// are replaced by parameter values; tokens without a declared parameter are listed as text.
struct SvgTemplateParam
{
    QString name;
    QString label;
    QString type = QStringLiteral("text");
    QString defaultValue;
};

struct SvgTemplateInfo
{
    QString id;           // built-in id, or the file path of a custom SVG
    QString name;
    QString resourcePath; // empty for custom files
    QList<SvgTemplateParam> params;
};

// Built-in tokens, re-evaluated every second: time, time12, date, countdown, countdownColor.
QStringList svgBuiltinTokens();

QList<SvgTemplateInfo> builtinSvgTemplates();

bool isBuiltinSvgTemplate(const QString &id);

// A built-in id resolves to its resource; anything else is read as a file path.
QByteArray svgTemplateBytes(const QString &idOrPath);

// Parses the metadata and appends undeclared tokens found in the document as text params.
SvgTemplateInfo svgTemplateInfo(const QString &idOrPath, const QByteArray &svg);

// Defaults of the template's params as a JSON object.
QJsonObject svgTemplateDefaults(const SvgTemplateInfo &info);

// True when the document uses any built-in token (so it needs a refresh every second).
bool svgUsesBuiltinTokens(const QByteArray &svg);

// Replaces {name} tokens with XML-escaped values; unknown names become empty.
QString substituteSvgTokens(const QString &svg, const QJsonObject &values);

} // namespace prism
