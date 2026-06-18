#include "ui/RemoteServerDialog.h"
#include "ui/QrCodeHelper.h"
#include "core/FirewallUtils.h"
#include "core/NetworkUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QComboBox>
#include <QNetworkInterface>
#include <QMessageBox>

RemoteServerDialog::RemoteServerDialog(RemoteControlServer *server, QWidget *parent)
    : QDialog(parent)
    , m_server(server)
{
    setWindowTitle(tr("Remote Control Server Settings"));
    setMinimumWidth(400);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *grpConfig = new QGroupBox(tr("Server Configuration"), this);
    QVBoxLayout *grpLayout = new QVBoxLayout(grpConfig);

    QLabel *lblBind = new QLabel(tr("Network Interface Binding:"), this);
    grpLayout->addWidget(lblBind);

    m_rbLocalhost = new QRadioButton(tr("Localhost only (127.0.0.1 - secure, local device only)"), this);
    m_rbNetwork = new QRadioButton(tr("All network interfaces (0.0.0.0 - allows remote access from phone)"), this);
    m_rbNetwork->setChecked(true);

    grpLayout->addWidget(m_rbLocalhost);
    grpLayout->addWidget(m_rbNetwork);

    m_lblPort = new QLabel(
        tr("Server port: %1").arg(RemoteControlServer::kPort), this);
    grpLayout->addWidget(m_lblPort);

    mainLayout->addWidget(grpConfig);

    m_qrPanel = new QWidget(this);
    auto *qrLayout = new QVBoxLayout(m_qrPanel);
    qrLayout->setContentsMargins(0, 0, 0, 0);

    m_qrLabel = new QLabel(m_qrPanel);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    qrLayout->addWidget(m_qrLabel);

    m_ifaceRow = new QWidget(m_qrPanel);
    auto *ifaceRow = new QHBoxLayout(m_ifaceRow);
    ifaceRow->addWidget(new QLabel(tr("QR network:"), m_ifaceRow));
    m_ifaceCombo = new QComboBox(m_ifaceRow);
    ifaceRow->addWidget(m_ifaceCombo, 1);
    qrLayout->addWidget(m_ifaceRow);

    m_lblQrUrl = new QLabel(m_qrPanel);
    m_lblQrUrl->setAlignment(Qt::AlignCenter);
    m_lblQrUrl->setWordWrap(true);
    m_lblQrUrl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    qrLayout->addWidget(m_lblQrUrl);

    m_lblQrHint = new QLabel(
        tr("Scan with your phone camera to open the remote console."), m_qrPanel);
    m_lblQrHint->setAlignment(Qt::AlignCenter);
    m_lblQrHint->setWordWrap(true);
    qrLayout->addWidget(m_lblQrHint);

    mainLayout->addWidget(m_qrPanel);
    m_qrPanel->setVisible(false);

    m_lblStatus = new QLabel(this);
    m_lblStatus->setWordWrap(true);
    m_lblStatus->setTextFormat(Qt::RichText);
    mainLayout->addWidget(m_lblStatus);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_btnStartStop = new QPushButton(this);
    connect(m_btnStartStop, &QPushButton::clicked, this, &RemoteServerDialog::onStartStopClicked);

    QPushButton *btnClose = new QPushButton(tr("Close"), this);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addStretch();
    btnLayout->addWidget(m_btnStartStop);
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);

    connect(m_rbLocalhost, &QRadioButton::toggled, this, &RemoteServerDialog::onBindingModeChanged);
    connect(m_rbNetwork,   &QRadioButton::toggled, this, &RemoteServerDialog::onBindingModeChanged);
    connect(m_ifaceCombo,  &QComboBox::currentIndexChanged, this, &RemoteServerDialog::onQrTargetChanged);

    refreshInterfaceList();
    updateInterfacePickerVisibility();
    updateUiState();
}

void RemoteServerDialog::refreshInterfaceList() {
    m_ifaces = NetworkUtils::listIpv4Interfaces();
    m_ifaceCombo->clear();
    for (const NetworkUtils::Ipv4Interface &iface : m_ifaces)
        m_ifaceCombo->addItem(iface.label);
    if (!m_ifaces.isEmpty())
        m_ifaceCombo->setCurrentIndex(NetworkUtils::defaultInterfaceIndex(m_ifaces));
}

void RemoteServerDialog::updateInterfacePickerVisibility() {
    const bool network = m_rbNetwork->isChecked();
    if (m_ifaceRow)
        m_ifaceRow->setVisible(network);
    m_ifaceCombo->setEnabled(network && m_ifaces.size() > 1);
}

void RemoteServerDialog::onBindingModeChanged() {
    updateInterfacePickerVisibility();
    updateQrCode();
}

void RemoteServerDialog::onQrTargetChanged() {
    updateQrCode();
}

QString RemoteServerDialog::remoteConsoleUrl() const {
    const quint16 port = RemoteControlServer::kPort;
    if (!m_server || !m_server->isRunning())
        return {};

    if (m_rbLocalhost->isChecked())
        return QStringLiteral("http://127.0.0.1:%1/").arg(port);

    if (m_ifaces.isEmpty())
        return {};

    const int idx = m_ifaceCombo->currentIndex();
    if (idx < 0 || idx >= m_ifaces.size())
        return {};

    return QStringLiteral("http://%1:%2/")
        .arg(m_ifaces[idx].address)
        .arg(port);
}

void RemoteServerDialog::updateQrCode() {
    if (!m_server || !m_server->isRunning()) {
        m_qrPanel->setVisible(false);
        return;
    }

    const QString url = remoteConsoleUrl();
    m_qrPanel->setVisible(true);
    updateInterfacePickerVisibility();

    if (url.isEmpty()) {
        m_qrLabel->clear();
        m_lblQrUrl->setText(tr("No network interface available for QR code."));
        return;
    }

    m_qrLabel->setPixmap(QrCodeHelper::toPixmap(url, 5));
    m_lblQrUrl->setText(url);
}

void RemoteServerDialog::updateUiState() {
    if (!m_server) return;

    const quint16 port = RemoteControlServer::kPort;

    if (m_server->isRunning()) {
        m_rbLocalhost->setEnabled(false);
        m_rbNetwork->setEnabled(false);
        m_ifaceCombo->setEnabled(m_rbNetwork->isChecked() && m_ifaces.size() > 1);

        QString urlList;
        QHostAddress addr = m_server->serverAddress();

        if (addr == QHostAddress::Any) {
            QList<QHostAddress> ipAddressesList = QNetworkInterface::allAddresses();
            for (const QHostAddress &ip : ipAddressesList) {
                if (ip.protocol() == QAbstractSocket::IPv4Protocol && ip != QHostAddress::LocalHost) {
                    urlList += QString("<br>• <a href=\"http://%1:%2\" style=\"color: #e5a93b;\">http://%1:%2</a>")
                                  .arg(ip.toString()).arg(port);
                }
            }
            urlList += QString("<br>• <a href=\"http://127.0.0.1:%1\" style=\"color: #e5a93b;\">http://127.0.0.1:%1</a>")
                          .arg(port);
        } else {
            urlList += QString("<br>• <a href=\"http://%1:%2\" style=\"color: #e5a93b;\">http://%1:%2</a>")
                          .arg(addr.toString()).arg(port);
        }

        m_lblStatus->setText(tr("<b>Server is RUNNING!</b> Access remotely at:%1").arg(urlList));
        m_lblStatus->setOpenExternalLinks(true);
        m_btnStartStop->setText(tr("Stop Server"));
        m_btnStartStop->setStyleSheet("background-color: #aa3333; color: white; padding: 6px 12px;");
    } else {
        m_rbLocalhost->setEnabled(true);
        m_rbNetwork->setEnabled(true);
        refreshInterfaceList();

        m_lblStatus->setText(tr("<b>Server is stopped.</b>"));
        m_btnStartStop->setText(tr("Start Server"));
        m_btnStartStop->setStyleSheet("background-color: #33aa33; color: white; padding: 6px 12px;");
    }

    updateQrCode();
}

void RemoteServerDialog::onStartStopClicked() {
    if (!m_server) return;

    const quint16 port = RemoteControlServer::kPort;

    if (m_server->isRunning()) {
        m_server->stopServer();
    } else {
        QHostAddress addr = m_rbLocalhost->isChecked() ? QHostAddress::LocalHost : QHostAddress::Any;

        if (addr != QHostAddress::LocalHost && FirewallUtils::detect().active) {
            QString fwErr;
            if (!FirewallUtils::ensurePortsOpen(this, {port}, fwErr,
                    tr("remote control access from your phone"))) {
                const auto cont = QMessageBox::warning(
                    this,
                    tr("Firewall"),
                    tr("Could not open firewall port.\n%1\n\n"
                       "Start the server anyway? Your phone may not be able to connect.")
                        .arg(fwErr.isEmpty() ? tr("Permission denied or cancelled.") : fwErr),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
                if (cont != QMessageBox::Yes)
                    return;
            }
        }

        if (!m_server->startServer(addr, port)) {
            FirewallUtils::releasePorts({port});
            QMessageBox::warning(this, tr("Server Error"),
                                 tr("Could not start remote control server on port %1.\n"
                                    "Please check if the port is already in use by another application.")
                                 .arg(port));
        }
    }
    updateUiState();
}
