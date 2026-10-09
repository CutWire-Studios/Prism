#include "core/sources/SvgTemplates.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

namespace prism {

namespace {

struct Builtin
{
    const char *id;
    const char *name;
};

constexpr Builtin kBuiltins[] = {
    {"clock", "Clock"},
    {"countdown_timer", "Countdown timer"},
    {"cricket_score_bar", "Cricket score bar"},
    {"cricket_score_table", "Cricket score table"},
    {"lower_third", "Lower third"},
    {"news_ticker", "News bar"},
    {"live_bug", "Live bug"},
};

QString builtinPath(const QString &id)
{
    return QStringLiteral(":/svg-templates/%1.svg").arg(id);
}

bool isBuiltinId(const QString &id)
{
    for (const Builtin &b : kBuiltins) {
        if (id == QLatin1String(b.id))
            return true;
    }
    return false;
}

QRegularExpression tokenRx()
{
    return QRegularExpression(QStringLiteral("\\{([a-zA-Z_][a-zA-Z0-9_]*)\\}"));
}

QString stripMetadata(const QString &svg)
{
    static const QRegularExpression rx(QStringLiteral("<metadata\\b.*?</metadata>"),
                                       QRegularExpression::DotMatchesEverythingOption);
    QString out = svg;
    out.remove(rx);
    return out;
}

QString valueText(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        const qint64 i = static_cast<qint64>(d);
        return static_cast<double>(i) == d ? QString::number(i) : QString::number(d);
    }
    default:
        return {};
    }
}

QString metadataJson(const QByteArray &svg)
{
    QXmlStreamReader xml(svg);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement
            && xml.name() == QLatin1String("metadata")
            && xml.attributes().value(QLatin1String("id")) == QLatin1String("prism"))
            return xml.readElementText();
    }
    return {};
}

} // namespace

QStringList svgBuiltinTokens()
{
    return {QStringLiteral("time"), QStringLiteral("time12"), QStringLiteral("date"),
            QStringLiteral("countdown"), QStringLiteral("countdownColor")};
}

bool isBuiltinSvgTemplate(const QString &id)
{
    return isBuiltinId(id);
}

QList<SvgTemplateInfo> builtinSvgTemplates()
{
    QList<SvgTemplateInfo> list;
    for (const Builtin &b : kBuiltins) {
        const QString id = QString::fromLatin1(b.id);
        SvgTemplateInfo info = svgTemplateInfo(id, svgTemplateBytes(id));
        info.name = QString::fromLatin1(b.name);
        list.append(info);
    }
    return list;
}

QByteArray svgTemplateBytes(const QString &idOrPath)
{
    QFile file(isBuiltinId(idOrPath) ? builtinPath(idOrPath) : idOrPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

SvgTemplateInfo svgTemplateInfo(const QString &idOrPath, const QByteArray &svg)
{
    SvgTemplateInfo info;
    info.id = idOrPath;
    if (isBuiltinId(idOrPath))
        info.resourcePath = builtinPath(idOrPath);

    const QJsonObject meta = QJsonDocument::fromJson(metadataJson(svg).toUtf8()).object();
    info.name = meta.value(QStringLiteral("name")).toString();
    QSet<QString> seen;
    for (const QJsonValue &v : meta.value(QStringLiteral("params")).toArray()) {
        const QJsonObject o = v.toObject();
        SvgTemplateParam p;
        p.name = o.value(QStringLiteral("name")).toString();
        if (p.name.isEmpty() || seen.contains(p.name))
            continue;
        seen.insert(p.name);
        p.label = o.value(QStringLiteral("label")).toString(p.name);
        p.type = o.value(QStringLiteral("type")).toString(QStringLiteral("text"));
        p.defaultValue = valueText(o.value(QStringLiteral("default")));
        info.params.append(p);
    }

    const QStringList builtin = svgBuiltinTokens();
    const QString body = stripMetadata(QString::fromUtf8(svg));
    QRegularExpressionMatchIterator it = tokenRx().globalMatch(body);
    while (it.hasNext()) {
        const QString name = it.next().captured(1);
        if (seen.contains(name) || builtin.contains(name))
            continue;
        seen.insert(name);
        SvgTemplateParam p;
        p.name = name;
        p.label = name;
        info.params.append(p);
    }
    return info;
}

QJsonObject svgTemplateDefaults(const SvgTemplateInfo &info)
{
    QJsonObject o;
    for (const SvgTemplateParam &p : info.params)
        o.insert(p.name, p.defaultValue);
    return o;
}

bool svgUsesBuiltinTokens(const QByteArray &svg)
{
    const QStringList builtin = svgBuiltinTokens();
    const QString body = stripMetadata(QString::fromUtf8(svg));
    QRegularExpressionMatchIterator it = tokenRx().globalMatch(body);
    while (it.hasNext()) {
        if (builtin.contains(it.next().captured(1)))
            return true;
    }
    return false;
}

QString substituteSvgTokens(const QString &svg, const QJsonObject &values)
{
    QString out;
    out.reserve(svg.size());
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = tokenRx().globalMatch(svg);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(svg).mid(last, m.capturedStart() - last);
        out += valueText(values.value(m.captured(1))).toHtmlEscaped().replace(QLatin1Char('"'),
                                                                                QStringLiteral("&quot;"));
        last = m.capturedEnd();
    }
    out += QStringView(svg).mid(last);
    return out;
}

} // namespace prism
