#include "ui/obs/ObsIntegration.h"
#include "ui/obs/ObsWebSocketClient.h"
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

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

void ObsIntegration::showConnectDialog(QWidget *parent) {
    if (!ObsWebSocketClient::isAvailable()) {
        QMessageBox::warning(parent, tr("OBS Integration"),
            tr("OBS integration requires Qt WebSockets (install qt6-websockets and rebuild)."));
        return;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Connect to OBS"));

    auto *hostEdit = new QLineEdit(m_host, &dlg);
    auto *portSpin = new QSpinBox(&dlg);
    portSpin->setRange(1, 65535);
    portSpin->setValue(m_port);
    auto *passEdit = new QLineEdit(m_password, &dlg);
    passEdit->setEchoMode(QLineEdit::Password);
    passEdit->setPlaceholderText(tr("obs-websocket password (leave empty if disabled)"));

    auto *statusLabel = new QLabel(
        isConnected() ? tr("Status: Connected") : tr("Status: Disconnected"), &dlg);

    auto *form = new QFormLayout;
    form->addRow(tr("Host:"), hostEdit);
    form->addRow(tr("Port:"), portSpin);
    form->addRow(tr("Password:"), passEdit);
    form->addRow(QString(), statusLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    auto *connectBtn = buttons->addButton(tr("Connect"), QDialogButtonBox::ActionRole);
    auto *disconnectBtn = buttons->addButton(tr("Disconnect"), QDialogButtonBox::ActionRole);
    disconnectBtn->setEnabled(isConnected());

    connect(connectBtn, &QPushButton::clicked, &dlg, [&]() {
        m_host     = hostEdit->text().trimmed();
        m_port     = static_cast<quint16>(portSpin->value());
        m_password = passEdit->text();
        saveSettings();
        m_client->connectToObs(m_host, m_port, m_password);
        statusLabel->setText(tr("Status: Connecting…"));
    });
    connect(disconnectBtn, &QPushButton::clicked, &dlg, [&]() {
        disconnectFromObs();
        statusLabel->setText(tr("Status: Disconnected"));
        disconnectBtn->setEnabled(false);
    });
    connect(m_client, &ObsWebSocketClient::connectedChanged, &dlg, [&](bool on) {
        statusLabel->setText(on ? tr("Status: Connected") : tr("Status: Disconnected"));
        disconnectBtn->setEnabled(on);
        if (on)
            QMessageBox::information(&dlg, tr("OBS"), tr("Connected to OBS WebSocket."));
    });
    connect(m_client, &ObsWebSocketClient::errorOccurred, &dlg, [&](const QString &err) {
        statusLabel->setText(tr("Status: Error — %1").arg(err));
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(new QLabel(
        tr("Requires OBS Studio with obs-websocket enabled (Tools → WebSocket Server Settings)."),
        &dlg));
    layout->addWidget(buttons);
    dlg.resize(420, 220);
    dlg.exec();
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

std::optional<QString> ObsIntegration::promptLinkClipObsScene(QWidget *parent, const QString &clipName,
                                                              const QString &currentScene)
{
    if (!isConnected()) {
        QMessageBox::information(parent, tr("OBS Scene Link"),
            tr("Connect to OBS first (View → Connect to OBS…)."));
        return std::nullopt;
    }

    QStringList options = sceneNames();
    options.prepend(tr("(None — don't switch OBS)"));

    int currentIdx = 0;
    if (!currentScene.isEmpty()) {
        const int idx = options.indexOf(currentScene);
        if (idx >= 0) currentIdx = idx;
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        parent, tr("OBS Scene Link"),
        tr("When \"%1\" is sent to a deck, switch OBS to:").arg(clipName),
        options, currentIdx, false, &ok);
    if (!ok) return std::nullopt;

    return (chosen == options.first()) ? QString{} : chosen;
}
