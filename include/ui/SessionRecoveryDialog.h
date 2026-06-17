#pragma once

#include <QDialog>
#include <QStringList>

/// Shown on startup when the previous session did not shut down cleanly.
class SessionRecoveryDialog : public QDialog {
    Q_OBJECT

public:
    enum class Choice {
        RecoverAutosave,
        RecoverBackup,
        StartFresh
    };

    explicit SessionRecoveryDialog(const QString &autosavePath,
                                   const QStringList &backupPaths,
                                   QWidget *parent = nullptr);

    Choice choice() const { return m_choice; }
    QString selectedBackupPath() const { return m_selectedBackupPath; }

private:
    Choice       m_choice = Choice::RecoverAutosave;
    QString      m_selectedBackupPath;
    QStringList  m_backupPaths;
};
