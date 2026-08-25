#include "mcp/McpSession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace prism::mcp {
namespace {

QString defaultSessionDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(base).filePath(QStringLiteral("prism"));
}

} // namespace

QString sessionFilePath()
{
    const QString override = qEnvironmentVariable("PRISM_MCP_SESSION_PATH");
    if (!override.isEmpty())
        return override;
    return QDir(defaultSessionDir()).filePath(QStringLiteral("mcp-session.json"));
}

bool writeSessionFile(quint16 port, const QString &token)
{
    const QString path = sessionFilePath();
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir))
        return false;

    const QJsonObject body{
        {QStringLiteral("port"), static_cast<int>(port)},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:%1/mcp").arg(port)},
        {QStringLiteral("token"), token},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
    };

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(body).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void removeSessionFile()
{
    QFile::remove(sessionFilePath());
}

bool readSessionFile(quint16 *port, QString *token, QString *error)
{
    QFile file(sessionFilePath());
    if (!file.exists()) {
        if (error) {
            *error = QStringLiteral(
                "Prism MCP is off. Open Prism and enable Agent access in Run → Agent Access.");
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not read the Prism MCP session file.");
        return false;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("Prism MCP session file is invalid.");
        return false;
    }
    const QJsonObject o = doc.object();
    const int p = o.value(QStringLiteral("port")).toInt();
    const QString t = o.value(QStringLiteral("token")).toString();
    if (p <= 0 || p > 65535 || t.isEmpty()) {
        if (error)
            *error = QStringLiteral("Prism MCP session file is incomplete.");
        return false;
    }
    if (port)
        *port = static_cast<quint16>(p);
    if (token)
        *token = t;
    return true;
}

} // namespace prism::mcp
