#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>

namespace prism::mcp {

using ToolHandler = std::function<QJsonObject(const QString &name, const QJsonObject &args)>;

// Handle a JSON-RPC request or batch. `toolbox` empty = homepage tools; otherwise that toolbox's ops.
// Notifications (no id) return a null QJsonValue.
QJsonValue handleJsonRpc(const QJsonValue &body, const QString &toolbox, const ToolHandler &handler);

QJsonArray toolsForEndpoint(const QString &toolbox);

} // namespace prism::mcp
