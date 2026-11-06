#include "app/obs/ObsIntegration.h"
#include "app/obs/ObsWebSocketClient.h"
#include <QDebug>
#include <QSettings>

ObsIntegration::ObsIntegration(QObject *parent)
    : QObject(parent)
    , m_client(new ObsWebSocketClient(this))
{
    loadSettings();

    connect(m_client, &ObsWebSocketClient::connectedChanged,
            this, &ObsIntegration::connectedChanged);
    connect(m_client, &ObsWebSocketClient::sceneListUpdated,
            this, &ObsIntegration::sceneListUpdated);
    connect(m_client, &ObsWebSocketClient::errorOccurred, this, [this](const QString &msg) {
        qWarning() << "OBS:" << msg;
        emit errorOccurred(msg);
    });
}

ObsIntegration::~ObsIntegration() = default;

bool ObsIntegration::isConnected() const {
    return ObsWebSocketClient::isAvailable() && m_client && m_client->isConnected();
}

QStringList ObsIntegration::sceneNames() const {
    return m_client ? m_client->sceneNames() : QStringList{};
}

void ObsIntegration::loadSettings() {
    QSettings s;
    s.beginGroup(QStringLiteral("obs"));
    m_host     = s.value(QStringLiteral("host"), m_host).toString();
    m_port     = static_cast<quint16>(s.value(QStringLiteral("port"), m_port).toUInt());
    m_password = s.value(QStringLiteral("password")).toString();
    s.endGroup();
}

void ObsIntegration::saveSettings() const {
    QSettings s;
    s.beginGroup(QStringLiteral("obs"));
    s.setValue(QStringLiteral("host"), m_host);
    s.setValue(QStringLiteral("port"), m_port);
    s.setValue(QStringLiteral("password"), m_password);
    s.endGroup();
}

void ObsIntegration::connectToObs(const QString &host, quint16 port, const QString &password) {
    m_host = host.trimmed();
    m_port = port;
    m_password = password;
    saveSettings();
    m_client->connectToObs(m_host, m_port, m_password);
}

void ObsIntegration::disconnectFromObs() {
    if (m_client)
        m_client->disconnectFromObs();
}

void ObsIntegration::refreshScenes() {
    if (m_client && m_client->isConnected())
        m_client->refreshSceneList();
}

void ObsIntegration::switchProgramScene(const QString &sceneName) {
    if (m_client && m_client->isConnected())
        m_client->setProgramScene(sceneName);
}

void ObsIntegration::onClipTriggered(const SourceDescriptor &desc) {
    if (desc.obsSceneName.isEmpty()) return;
    switchProgramScene(desc.obsSceneName);
}
