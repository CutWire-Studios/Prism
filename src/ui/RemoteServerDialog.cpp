#include "ui/RemoteServerDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
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

    // Group Box for Server config
    QGroupBox *grpConfig = new QGroupBox(tr("Server Configuration"), this);
    QVBoxLayout *grpLayout = new QVBoxLayout(grpConfig);

    QLabel *lblBind = new QLabel(tr("Network Interface Binding:"), this);
    grpLayout->addWidget(lblBind);

    m_rbLocalhost = new QRadioButton(tr("Localhost only (127.0.0.1 - secure, local device only)"), this);
    m_rbNetwork = new QRadioButton(tr("All network interfaces (0.0.0.0 - allows remote access from phone)"), this);
    m_rbNetwork->setChecked(true); // default to network so remote access is easy

    grpLayout->addWidget(m_rbLocalhost);
    grpLayout->addWidget(m_rbNetwork);

    QHBoxLayout *portLayout = new QHBoxLayout();
    QLabel *lblPort = new QLabel(tr("Server Port:"), this);
    m_sbPort = new QSpinBox(this);
    m_sbPort->setRange(1024, 65535);
    m_sbPort->setValue(8080); // default port

    portLayout->addWidget(lblPort);
    portLayout->addWidget(m_sbPort);
    portLayout->addStretch();
    grpLayout->addLayout(portLayout);

    mainLayout->addWidget(grpConfig);

    // Status / IP display
    m_lblStatus = new QLabel(this);
    m_lblStatus->setWordWrap(true);
    m_lblStatus->setTextFormat(Qt::RichText);
    mainLayout->addWidget(m_lblStatus);

    // Action button
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_btnStartStop = new QPushButton(this);
    connect(m_btnStartStop, &QPushButton::clicked, this, &RemoteServerDialog::onStartStopClicked);

    QPushButton *btnClose = new QPushButton(tr("Close"), this);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addStretch();
    btnLayout->addWidget(m_btnStartStop);
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);

    updateUiState();
}

void RemoteServerDialog::updateUiState() {
    if (!m_server) return;

    if (m_server->isRunning()) {
        m_rbLocalhost->setEnabled(false);
        m_rbNetwork->setEnabled(false);
        m_sbPort->setEnabled(false);

        QString urlList;
        QHostAddress addr = m_server->serverAddress();
        quint16 port = m_server->serverPort();

        if (addr == QHostAddress::Any) {
            QList<QHostAddress> ipAddressesList = QNetworkInterface::allAddresses();
            for (const QHostAddress &ip : ipAddressesList) {
                if (ip.protocol() == QAbstractSocket::IPv4Protocol && ip != QHostAddress::LocalHost) {
                    urlList += QString("<br>• <a href=\"http://%1:%2\" style=\"color: #e5a93b;\">http://%1:%2</a>").arg(ip.toString()).arg(port);
                }
            }
            urlList += QString("<br>• <a href=\"http://127.0.0.1:%1\" style=\"color: #e5a93b;\">http://127.0.0.1:%1</a>").arg(port);
        } else {
            urlList += QString("<br>• <a href=\"http://%1:%2\" style=\"color: #e5a93b;\">http://%1:%2</a>").arg(addr.toString()).arg(port);
        }

        m_lblStatus->setText(tr("<b>Server is RUNNING!</b> Access remotely at:%1").arg(urlList));
        m_lblStatus->setOpenExternalLinks(true);
        m_btnStartStop->setText(tr("Stop Server"));
        m_btnStartStop->setStyleSheet("background-color: #aa3333; color: white; padding: 6px 12px;");
    } else {
        m_rbLocalhost->setEnabled(true);
        m_rbNetwork->setEnabled(true);
        m_sbPort->setEnabled(true);

        m_lblStatus->setText(tr("<b>Server is stopped.</b>"));
        m_btnStartStop->setText(tr("Start Server"));
        m_btnStartStop->setStyleSheet("background-color: #33aa33; color: white; padding: 6px 12px;");
    }
}

void RemoteServerDialog::onStartStopClicked() {
    if (!m_server) return;

    if (m_server->isRunning()) {
        m_server->stopServer();
    } else {
        QHostAddress addr = m_rbLocalhost->isChecked() ? QHostAddress::LocalHost : QHostAddress::Any;
        quint16 port = static_cast<quint16>(m_sbPort->value());
        if (!m_server->startServer(addr, port)) {
            QMessageBox::warning(this, tr("Server Error"),
                                 tr("Could not start remote control server on port %1.\n"
                                    "Please check if the port is already in use by another application.")
                                 .arg(port));
        }
    }
    updateUiState();
}

void RemoteServerDialog::accept() {
    if (m_server && m_server->isRunning()) {
        m_server->stopServer();
    }
    QDialog::accept();
}

void RemoteServerDialog::reject() {
    if (m_server && m_server->isRunning()) {
        m_server->stopServer();
    }
    QDialog::reject();
}
