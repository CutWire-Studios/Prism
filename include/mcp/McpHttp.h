#pragma once

#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;

namespace prism::mcp {

class McpHttp : public QObject
{
    Q_OBJECT

public:
    using RpcHandler = std::function<QJsonValue(const QString &path, const QJsonValue &body)>;

    explicit McpHttp(QObject *parent = nullptr);

    void setToken(const QString &token) { m_token = token; }
    void setRpcHandler(RpcHandler handler) { m_handler = std::move(handler); }

    quint16 port() const { return m_port; }

public slots:
    bool listen(quint16 preferredPort);
    void close();

signals:
    void listening(quint16 port);
    void failed(const QString &error);

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void handleRequest(QTcpSocket *socket, const QByteArray &header, const QByteArray &body);
    bool authorized(const QByteArray &header) const;

    QTcpServer *m_server = nullptr;
    QString m_token;
    RpcHandler m_handler;
    quint16 m_port = 0;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

} // namespace prism::mcp
