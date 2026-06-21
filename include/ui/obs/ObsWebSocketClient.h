#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <functional>

#ifdef SWITCHX_HAVE_OBS_WS
class QWebSocket;
#endif

/// Minimal obs-websocket v5 client (scene list + program scene switching).
class ObsWebSocketClient : public QObject {
    Q_OBJECT

public:
    explicit ObsWebSocketClient(QObject *parent = nullptr);
    ~ObsWebSocketClient() override;

    static bool isAvailable();

    bool    isConnected() const { return m_identified; }
    QString lastError() const { return m_lastError; }

    void connectToObs(const QString &host, quint16 port, const QString &password);
    void disconnectFromObs();

    void refreshSceneList();
    void setProgramScene(const QString &sceneName);

    QStringList sceneNames() const { return m_sceneNames; }

signals:
    void connectedChanged(bool connected);
    void sceneListUpdated(const QStringList &scenes);
    void programSceneChanged(const QString &sceneName);
    void errorOccurred(const QString &message);

private:
#ifdef SWITCHX_HAVE_OBS_WS
    void onSocketConnected();
    void onSocketDisconnected();
    void onTextMessageReceived(const QString &message);
    void sendIdentify(const QJsonObject &helloData, const QString &password);
    void sendRequest(const QString &requestType, const QJsonObject &requestData,
                     std::function<void(const QJsonObject &)> handler = {});

    static QString buildAuthString(const QString &password,
                                   const QString &salt,
                                   const QString &challenge);
    static QString nextRequestId();

    QWebSocket *m_socket = nullptr;

    struct PendingRequest {
        QString requestType;
        std::function<void(const QJsonObject &)> handler;
    };
    QHash<QString, PendingRequest> m_pending;
#endif

    bool        m_identified = false;
    QString     m_lastError;
    QString     m_password;
    QStringList m_sceneNames;
    QString     m_currentProgramScene;
};
