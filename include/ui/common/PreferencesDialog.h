#pragma once

#include <QDialog>

class QComboBox;
class QLabel;

/// Playback & hardware preferences: video decoder, zero-copy GPU import and the recording
/// encoder. Decode mode applies immediately to every open clip; zero-copy needs a restart.
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

private:
    void updateDecodeWarning();
    void apply();

    QComboBox *m_decodeCombo = nullptr;
    QComboBox *m_zeroCopyCombo = nullptr;
    QComboBox *m_encoderCombo = nullptr;
    QLabel *m_decodeWarning = nullptr;
};
