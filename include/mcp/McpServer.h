#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <memory>

class MainWindow;
class QThread;

namespace prism::mcp {
class McpHttp;
class McpDispatcher;

class McpServer : public QObject
{
    Q_OBJECT

public:
    static constexpr quint16 kPreferredPort = 38471;

    explicit McpServer(MainWindow *window, QObject *parent = nullptr);
    ~McpServer() override;

    bool running() const { return m_running; }
    quint16 port() const { return m_port; }
    QString token() const { return m_token; }
    QString url() const;
    QString error() const { return m_error; }

    QString cursorSnippet() const;
    QString claudeCommand() const;
    QString stdioSnippet() const;

public slots:
    bool start();
    void stop();
    QJsonValue handleRpc(const QString &toolbox, const QJsonValue &body);

signals:
    void runningChanged();
    void errorChanged();

private:
    QString makeToken() const;
    QJsonObject dispatchTool(const QString &name, const QJsonObject &args);

    MainWindow *m_window = nullptr;
    std::unique_ptr<McpDispatcher> m_dispatcher;
    QThread *m_thread = nullptr;
    McpHttp *m_http = nullptr;
    QString m_token;
    QString m_error;
    quint16 m_port = 0;
    bool m_running = false;
};

} // namespace prism::mcp
