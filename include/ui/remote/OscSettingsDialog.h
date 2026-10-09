#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QSpinBox;

/// Edits the osc/* settings read by OscServer::applySettings().
class OscSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OscSettingsDialog(QWidget *parent = nullptr);

private:
    void apply();

    QCheckBox *m_enabled = nullptr;
    QSpinBox *m_port = nullptr;
    QLineEdit *m_feedbackHost = nullptr;
    QSpinBox *m_feedbackPort = nullptr;
};
