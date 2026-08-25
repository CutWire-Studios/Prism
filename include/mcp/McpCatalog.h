#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace prism::mcp {

QStringList toolboxNames();
QJsonObject catalogPayload();
QJsonObject toolboxPayload(const QString &name);
QJsonArray homepageTools();
QJsonArray toolboxDirectTools(const QString &name);
bool isHomepageTool(const QString &name);
bool isKnownOp(const QString &name);
bool isReadOnlyOp(const QString &name);
QString toolboxForOp(const QString &name);
QString homepageHtml();
QString agentGuideText();

} // namespace prism::mcp
