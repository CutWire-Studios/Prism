#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include "core/sources/SourceDescriptor.h"

class ObsWebSocketClient;

/// High-level OBS integration: connection settings, scene switching, clip hooks.
class ObsIntegration : public QObject {
    Q_OBJECT

public:
    explicit ObsIntegration(QObject *parent = nullptr);
    ~ObsIntegration() override;

    bool isConnected() const;

    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    QStringList sceneNames() const;

    QString password() const { return m_password; }
    /// Stores the endpoint and starts connecting; connectedChanged / errorOccurred report the outcome.
    void connectToObs(const QString &host, quint16 port, const QString &password);
    void disconnectFromObs();

    void refreshScenes();
    void switchProgramScene(const QString &sceneName);

    /// If the descriptor has obsSceneName set, switch OBS to that scene.
    void onClipTriggered(const SourceDescriptor &desc);


signals:
    void connectedChanged(bool connected);
    void sceneListUpdated(const QStringList &scenes);
    void errorOccurred(const QString &message);

private:
    void saveSettings() const;
    void loadSettings();

    ObsWebSocketClient *m_client = nullptr;
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 4455;
    QString m_password;
};
