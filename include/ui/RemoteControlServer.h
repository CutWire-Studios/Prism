#pragma once

#include <QTcpServer>
#include <QHostAddress>
#include <QPointer>
#include "ui/TransitionController.h"

class QSlider;
class QTcpSocket;
class MainWindow;

class RemoteControlServer : public QTcpServer {
    Q_OBJECT
public:
    explicit RemoteControlServer(MainWindow *mainWindow, TransitionController *transitionCtrl, QSlider *faderSlider, QObject *parent = nullptr);
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
    void sendJsonResponse(QTcpSocket *socket, const QByteArray &json);
    void sendTextResponse(QTcpSocket *socket, const QString &text, int statusCode);
    QString getWebPageHtml() const;

    QPointer<MainWindow> m_mainWindow;
    QPointer<TransitionController> m_transitionCtrl;
    QPointer<QSlider> m_faderSlider;
    bool m_isRunning = false;
    QHostAddress m_serverAddress;
    quint16 m_serverPort = 8080;
};
