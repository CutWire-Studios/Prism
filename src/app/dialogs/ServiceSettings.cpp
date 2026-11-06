#include "app/dialogs/ServiceSettings.h"

#include "app/AppController.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpServer.h"
#include "ui/obs/ObsIntegration.h"
#include "ui/obs/ObsWebSocketClient.h"
#include "ui/remote/OscServer.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSettings>

// ── OscSettings ───────────────────────────────────────────────────────────────

OscSettings::OscSettings(QObject *parent)
    : QObject(parent)
{
    load();
}

void OscSettings::load()
{
    QSettings s;
    m_enabled = s.value(QStringLiteral("osc/enabled"), false).toBool();
    m_port = s.value(QStringLiteral("osc/port"), OscServer::kDefaultPort).toInt();
    m_feedbackHost = s.value(QStringLiteral("osc/feedbackHost")).toString();
    m_feedbackPort = s.value(QStringLiteral("osc/feedbackPort"), OscServer::kDefaultFeedbackPort).toInt();
    emit changed();
}

void OscSettings::apply()
{
    QSettings s;
    s.setValue(QStringLiteral("osc/enabled"), m_enabled);
    s.setValue(QStringLiteral("osc/port"), m_port);
    s.setValue(QStringLiteral("osc/feedbackHost"), m_feedbackHost.trimmed());
    s.setValue(QStringLiteral("osc/feedbackPort"), m_feedbackPort);
    if (auto *app = AppController::instance())
        app->applyOscSettings();
}

void OscSettings::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    emit changed();
}

void OscSettings::setPort(int port)
{
    port = qBound(1, port, 65535);
    if (port == m_port)
        return;
    m_port = port;
    emit changed();
}

void OscSettings::setFeedbackHost(const QString &host)
{
    if (host == m_feedbackHost)
        return;
    m_feedbackHost = host;
    emit changed();
}

void OscSettings::setFeedbackPort(int port)
{
    port = qBound(1, port, 65535);
    if (port == m_feedbackPort)
        return;
    m_feedbackPort = port;
    emit changed();
}

// ── McpAccess ─────────────────────────────────────────────────────────────────

McpAccess::McpAccess(QObject *parent)
    : QObject(parent)
{
    if (auto *server = AppController::instance() ? AppController::instance()->mcpServer() : nullptr) {
        connect(server, &prism::mcp::McpServer::runningChanged, this, &McpAccess::changed);
        connect(server, &prism::mcp::McpServer::errorChanged, this, &McpAccess::changed);
    }
}

bool McpAccess::enabled() const
{
    auto *app = AppController::instance();
    return app && app->mcpEnabled();
}

void McpAccess::setEnabled(bool on)
{
    if (auto *app = AppController::instance())
        app->setMcpEnabled(on);
    emit changed();
}

QString McpAccess::url() const
{
    auto *app = AppController::instance();
    return app && app->mcpServer() ? app->mcpServer()->url() : QString();
}

QString McpAccess::token() const
{
    auto *app = AppController::instance();
    return app && app->mcpServer() ? app->mcpServer()->token() : QString();
}

QString McpAccess::error() const
{
    auto *app = AppController::instance();
    return app && app->mcpServer() ? app->mcpServer()->error() : QString();
}

void McpAccess::copyCursor() const
{
    if (auto *app = AppController::instance(); app && app->mcpServer())
        QGuiApplication::clipboard()->setText(app->mcpServer()->cursorSnippet());
}

void McpAccess::copyClaude() const
{
    if (auto *app = AppController::instance(); app && app->mcpServer())
        QGuiApplication::clipboard()->setText(app->mcpServer()->claudeCommand());
}

void McpAccess::copyStdio() const
{
    if (auto *app = AppController::instance(); app && app->mcpServer())
        QGuiApplication::clipboard()->setText(app->mcpServer()->stdioSnippet());
}

void McpAccess::copyGuide() const
{
    QString guide = prism::mcp::agentGuideText();
    auto *app = AppController::instance();
    if (app && app->mcpServer() && app->mcpServer()->running()) {
        guide += QStringLiteral("\nThis session: %1\nAuthorization: Bearer %2\n")
                     .arg(app->mcpServer()->url(), app->mcpServer()->token());
    }
    QGuiApplication::clipboard()->setText(guide);
}

// ── ObsConnection ─────────────────────────────────────────────────────────────

ObsConnection::ObsConnection(QObject *parent)
    : QObject(parent)
{
    if (auto *obs = AppController::instance() ? AppController::instance()->obsIntegration() : nullptr) {
        connect(obs, &ObsIntegration::connectedChanged, this, [this](bool) {
            m_connecting = false;
            m_error.clear();
            emit changed();
        });
        connect(obs, &ObsIntegration::errorOccurred, this, [this](const QString &message) {
            m_connecting = false;
            m_error = message;
            emit changed();
        });
    }
}

bool ObsConnection::available() const { return ObsWebSocketClient::isAvailable(); }

bool ObsConnection::connected() const
{
    auto *app = AppController::instance();
    return app && app->obsConnected();
}

QString ObsConnection::host() const
{
    auto *app = AppController::instance();
    return app && app->obsIntegration() ? app->obsIntegration()->host() : QString();
}

int ObsConnection::port() const
{
    auto *app = AppController::instance();
    return app && app->obsIntegration() ? app->obsIntegration()->port() : 4455;
}

QString ObsConnection::password() const
{
    auto *app = AppController::instance();
    return app && app->obsIntegration() ? app->obsIntegration()->password() : QString();
}

void ObsConnection::connectToObs(const QString &host, int port, const QString &password)
{
    auto *app = AppController::instance();
    if (!app || !app->obsIntegration())
        return;
    m_error.clear();
    m_connecting = true;
    app->obsIntegration()->connectToObs(host, static_cast<quint16>(qBound(1, port, 65535)), password);
    emit changed();
}

void ObsConnection::disconnectFromObs()
{
    auto *app = AppController::instance();
    if (!app || !app->obsIntegration())
        return;
    m_connecting = false;
    app->obsIntegration()->disconnectFromObs();
    emit changed();
}
