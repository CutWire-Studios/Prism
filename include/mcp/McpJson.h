#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace prism::mcp {

inline QJsonObject ok(QJsonObject extra = {})
{
    extra.insert(QStringLiteral("ok"), true);
    return extra;
}

inline QJsonObject err(const char *code, const QString &detail = {})
{
    QJsonObject o{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QString::fromUtf8(code)}};
    if (!detail.isEmpty())
        o.insert(QStringLiteral("detail"), detail);
    return o;
}

inline QJsonObject objectSchema(const QJsonObject &properties, const QStringList &required = {})
{
    QJsonObject s{{QStringLiteral("type"), QStringLiteral("object")},
                  {QStringLiteral("properties"), properties}};
    if (!required.isEmpty()) {
        QJsonArray req;
        for (const QString &key : required)
            req.append(key);
        s.insert(QStringLiteral("required"), req);
    }
    return s;
}

inline QJsonObject stringProp(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), description}};
}

inline QJsonObject numberProp(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("number")},
            {QStringLiteral("description"), description}};
}

inline QJsonObject integerProp(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), description}};
}

inline QJsonObject boolProp(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), description}};
}

inline QJsonObject objectProp(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("description"), description}};
}

inline QJsonObject arrayProp(const QJsonObject &items, const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("items"), items},
            {QStringLiteral("description"), description}};
}

inline QJsonObject enumProp(const QString &description, const QStringList &values)
{
    QJsonArray enumValues;
    for (const QString &value : values)
        enumValues.append(value);
    return {{QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), enumValues},
            {QStringLiteral("description"), description}};
}

inline QJsonObject propWithDefault(QJsonObject prop, const QJsonValue &defaultValue)
{
    prop.insert(QStringLiteral("default"), defaultValue);
    return prop;
}

inline QJsonObject toolAnnotations(bool readOnly = false, bool destructive = false,
                                   bool idempotent = false)
{
    QJsonObject annotations;
    if (readOnly)
        annotations.insert(QStringLiteral("readOnlyHint"), true);
    if (destructive)
        annotations.insert(QStringLiteral("destructiveHint"), true);
    if (idempotent)
        annotations.insert(QStringLiteral("idempotentHint"), true);
    return annotations;
}

inline QJsonObject toolDef(const QString &name, const QString &description,
                           const QJsonObject &inputSchema, const QJsonObject &annotations = {})
{
    QJsonObject tool{{QStringLiteral("name"), name},
                     {QStringLiteral("description"), description},
                     {QStringLiteral("inputSchema"), inputSchema}};
    if (!annotations.isEmpty())
        tool.insert(QStringLiteral("annotations"), annotations);
    return tool;
}

inline QJsonObject textResult(const QJsonObject &payload, bool isError = false)
{
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QJsonObject contentItem{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("text"), QString::fromUtf8(json)}};
    return {{QStringLiteral("content"), QJsonArray{contentItem}},
            {QStringLiteral("isError"),
             isError || payload.value(QStringLiteral("ok")).toBool(true) == false}};
}

} // namespace prism::mcp
