#include "mcp/McpProtocol.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpJson.h"
#include "release.h"

namespace prism::mcp {
namespace {

QJsonObject jsonRpcError(const QJsonValue &id, int code, const QString &message)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

QJsonObject jsonRpcOk(const QJsonValue &id, const QJsonValue &result)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("result"), result}};
}

QJsonObject initializeResult()
{
    return {
        {QStringLiteral("protocolVersion"), QStringLiteral("2025-03-26")},
        {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
        {QStringLiteral("serverInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("prism")},
                     {QStringLiteral("version"), QStringLiteral(PRISM_VERSION_STRING)}}},
        {QStringLiteral("instructions"),
         QStringLiteral(
             "Call catalog first, then toolbox({name}) for schemas, then apply({ops}) to mutate. "
             "Homepage /mcp exposes catalog, toolbox, apply, inspect, capture. "
             "Pinned /mcp/{toolbox} lists that toolbox's ops directly (no catalog/apply there). "
             "Always prefer clip id from inspect({clips:true}). "
             "Times are seconds. Fader is 0 = deck A, 100 = deck B. "
             "capture() returns a JPEG still of program output.")},
    };
}

QJsonValue handleOne(const QJsonObject &req, const QString &toolbox, const ToolHandler &handler)
{
    const QJsonValue id = req.value(QStringLiteral("id"));
    const QString method = req.value(QStringLiteral("method")).toString();
    const QJsonObject params = req.value(QStringLiteral("params")).toObject();
    const bool notification = !req.contains(QStringLiteral("id"));

    if (method == QLatin1String("initialize"))
        return jsonRpcOk(id, initializeResult());
    if (method == QLatin1String("notifications/initialized") || method == QLatin1String("initialized"))
        return notification ? QJsonValue(QJsonValue::Undefined) : jsonRpcOk(id, QJsonObject{});
    if (method == QLatin1String("ping"))
        return jsonRpcOk(id, QJsonObject{});
    if (method == QLatin1String("tools/list"))
        return jsonRpcOk(id, QJsonObject{{QStringLiteral("tools"), toolsForEndpoint(toolbox)}});
    if (method == QLatin1String("resources/list"))
        return jsonRpcOk(id, QJsonObject{{QStringLiteral("resources"), QJsonArray{}}});
    if (method == QLatin1String("prompts/list"))
        return jsonRpcOk(id, QJsonObject{{QStringLiteral("prompts"), QJsonArray{}}});

    if (method == QLatin1String("tools/call")) {
        const QString name = params.value(QStringLiteral("name")).toString();
        const QJsonObject args = params.value(QStringLiteral("arguments")).toObject();
        if (name.isEmpty())
            return jsonRpcError(id, -32602, QStringLiteral("Missing tool name"));
        if (!handler)
            return jsonRpcError(id, -32603, QStringLiteral("No tool handler"));

        const bool homepage = toolbox.isEmpty();
        if (homepage && !isHomepageTool(name) && !isKnownOp(name))
            return jsonRpcOk(id, textResult(err("unknown_op", name), true));
        if (!homepage) {
            if (isHomepageTool(name) && name != QLatin1String("inspect")
                && name != QLatin1String("capture"))
                return jsonRpcOk(
                    id,
                    textResult(err("wrong_endpoint", QStringLiteral("Use /mcp for %1").arg(name)),
                               true));
            if (isKnownOp(name) && toolboxForOp(name) != toolbox)
                return jsonRpcOk(id,
                                 textResult(err("wrong_toolbox",
                                                QStringLiteral("%1 belongs to %2")
                                                    .arg(name, toolboxForOp(name))),
                                            true));
        }

        const QJsonObject result = handler(name, args);
        return jsonRpcOk(id, result);
    }

    if (notification)
        return QJsonValue(QJsonValue::Undefined);
    return jsonRpcError(id, -32601, QStringLiteral("Unknown method: %1").arg(method));
}

} // namespace

QJsonArray toolsForEndpoint(const QString &toolbox)
{
    if (toolbox.isEmpty())
        return homepageTools();
    return toolboxDirectTools(toolbox);
}

QJsonValue handleJsonRpc(const QJsonValue &body, const QString &toolbox, const ToolHandler &handler)
{
    if (body.isArray()) {
        QJsonArray out;
        for (const QJsonValue &item : body.toArray()) {
            if (!item.isObject())
                continue;
            const QJsonValue one = handleOne(item.toObject(), toolbox, handler);
            if (!one.isUndefined())
                out.append(one);
        }
        if (out.isEmpty())
            return QJsonValue(QJsonValue::Undefined);
        return out;
    }
    if (body.isObject())
        return handleOne(body.toObject(), toolbox, handler);
    return jsonRpcError(QJsonValue::Null, -32700, QStringLiteral("Parse error"));
}

} // namespace prism::mcp
