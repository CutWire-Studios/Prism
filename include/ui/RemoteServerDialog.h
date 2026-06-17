#pragma once

#include <QDialog>
#include <QPointer>
#include "ui/RemoteControlServer.h"

class QRadioButton;
class QSpinBox;
class QLabel;
class QPushButton;

class RemoteServerDialog : public QDialog {
    Q_OBJECT
public:
    explicit RemoteServerDialog(RemoteControlServer *server, QWidget *parent = nullptr);
    void accept() override;
    void reject() override;

private slots:
    void onStartStopClicked();

private:
    void updateUiState();

    QPointer<RemoteControlServer> m_server;

    QRadioButton *m_rbLocalhost;
    QRadioButton *m_rbNetwork;
    QSpinBox     *m_sbPort;
    QLabel       *m_lblStatus;
    QPushButton  *m_btnStartStop;
};
