#pragma once

#include <QTcpServer>
#include <QHostAddress>
#include <QPointer>

class AppController;
class QTcpSocket;
class TransitionController;

/// TCP server that lets a remote client (phone/companion app) drive the live
/// transition and T-bar fader. Speaks the line protocol in RemoteProtocol.
class RemoteControlServer : public QTcpServer {
    Q_OBJECT
public:
    static constexpr quint16 kPort = 38470;

    explicit RemoteControlServer(AppController *app, QObject *parent = nullptr);
    ~RemoteControlServer() override;

    bool startServer(const QHostAddress &address, quint16 port);
    void stopServer();

    bool isRunning() const { return m_isRunning; }
    QHostAddress serverAddress() const { return m_serverAddress; }
    quint16 serverPort() const { return m_serverPort; }

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void sendHtmlResponse(QTcpSocket *socket);
    void sendCamHtmlResponse(QTcpSocket *socket, const QString &token, quint16 sigPort);
    void sendJsonResponse(QTcpSocket *socket, const QByteArray &json);
    void sendTextResponse(QTcpSocket *socket, const QString &text, int statusCode);
    QString getWebPageHtml() const;
    TransitionController *transitions() const;

    QPointer<AppController> m_mainWindow;
    bool m_isRunning = false;
    QHostAddress m_serverAddress;
    quint16 m_serverPort = kPort;
};
