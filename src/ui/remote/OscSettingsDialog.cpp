#include "ui/remote/OscSettingsDialog.h"

#include "ui/remote/OscServer.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

OscSettingsDialog::OscSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("OSC Server"));
    setMinimumWidth(400);

    QSettings settings;
    m_enabled = new QCheckBox(tr("Listen for OSC messages"), this);
    m_enabled->setChecked(settings.value(QStringLiteral("osc/enabled"), false).toBool());
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(settings.value(QStringLiteral("osc/port"), OscServer::kDefaultPort).toInt());
    m_feedbackHost = new QLineEdit(settings.value(QStringLiteral("osc/feedbackHost")).toString(), this);
    m_feedbackHost->setPlaceholderText(tr("Disabled"));
    m_feedbackPort = new QSpinBox(this);
    m_feedbackPort->setRange(1, 65535);
    m_feedbackPort->setValue(
        settings.value(QStringLiteral("osc/feedbackPort"), OscServer::kDefaultFeedbackPort).toInt());

    auto *note = new QLabel(
        tr("Starts automatically with Prism while enabled. When a feedback host is set, "
           "state changes are sent there as OSC messages."), this);
    note->setWordWrap(true);
    note->setEnabled(false);

    auto *form = new QFormLayout;
    form->addRow(QString(), m_enabled);
    form->addRow(tr("UDP port:"), m_port);
    form->addRow(tr("Feedback host:"), m_feedbackHost);
    form->addRow(tr("Feedback port:"), m_feedbackPort);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addStretch();
    layout->addWidget(buttons);
}

void OscSettingsDialog::apply()
{
    QSettings settings;
    settings.setValue(QStringLiteral("osc/enabled"), m_enabled->isChecked());
    settings.setValue(QStringLiteral("osc/port"), m_port->value());
    settings.setValue(QStringLiteral("osc/feedbackHost"), m_feedbackHost->text().trimmed());
    settings.setValue(QStringLiteral("osc/feedbackPort"), m_feedbackPort->value());
}
