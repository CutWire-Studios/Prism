#pragma once

#include <QDialog>
#include <QPointer>
#include <QList>
#include "ui/RemoteControlServer.h"
#include "core/NetworkUtils.h"

class QRadioButton;
class QLabel;
class QPushButton;
class QComboBox;
class QWidget;

class RemoteServerDialog : public QDialog {
    Q_OBJECT
public:
    explicit RemoteServerDialog(RemoteControlServer *server, QWidget *parent = nullptr);

private slots:
    void onStartStopClicked();
    void onBindingModeChanged();
    void onQrTargetChanged();

private:
    void updateUiState();
    void refreshInterfaceList();
    void updateInterfacePickerVisibility();
    QString remoteConsoleUrl() const;
    void updateQrCode();

    QPointer<RemoteControlServer> m_server;

    QRadioButton *m_rbLocalhost = nullptr;
    QRadioButton *m_rbNetwork   = nullptr;
    QLabel       *m_lblPort     = nullptr;
    QLabel       *m_lblStatus   = nullptr;
    QPushButton  *m_btnStartStop = nullptr;
    QWidget      *m_qrPanel     = nullptr;
    QLabel       *m_qrLabel     = nullptr;
    QLabel       *m_lblQrUrl    = nullptr;
    QLabel       *m_lblQrHint   = nullptr;
    QWidget      *m_ifaceRow    = nullptr;
    QComboBox    *m_ifaceCombo  = nullptr;
    QList<NetworkUtils::Ipv4Interface> m_ifaces;
};
