#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>
#include "core/sources/SourceDescriptor.h"

class ObsWebSocketClient;
class QWidget;

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

    void showConnectDialog(QWidget *parent);
    void disconnectFromObs();

    void refreshScenes();
    void switchProgramScene(const QString &sceneName);

    /// If the descriptor has obsSceneName set, switch OBS to that scene.
    void onClipTriggered(const SourceDescriptor &desc);

    /// nullopt = cancelled; empty string = clear link; otherwise scene name.
    std::optional<QString> promptLinkClipObsScene(QWidget *parent, const QString &clipName,
                                                  const QString &currentScene);

signals:
    void connectedChanged(bool connected);
    void sceneListUpdated(const QStringList &scenes);

private:
    void saveSettings() const;
    void loadSettings();

    ObsWebSocketClient *m_client = nullptr;
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 4455;
    QString m_password;
};
