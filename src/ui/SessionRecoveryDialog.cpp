#include "ui/SessionRecoveryDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QButtonGroup>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

namespace {

QString formatSessionTimestamp(const QString &path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QString savedAt = doc.object().value(QStringLiteral("savedAt")).toString();
        if (!savedAt.isEmpty()) {
            const QDateTime dt = QDateTime::fromString(savedAt, Qt::ISODate);
            if (dt.isValid())
                return dt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        }
    }
    const QFileInfo info(path);
    return info.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
}

} // namespace

SessionRecoveryDialog::SessionRecoveryDialog(const QString &autosavePath,
                                             const QStringList &backupPaths,
                                             QWidget *parent)
    : QDialog(parent)
    , m_backupPaths(backupPaths)
{
    setWindowTitle(tr("Recover Session"));
    setMinimumWidth(480);

    auto *intro = new QLabel(
        tr("SwitchX did not shut down cleanly last time. "
           "You can recover your last autosaved session or choose a backup."),
        this);
    intro->setWordWrap(true);

    auto *autosaveRadio = new QRadioButton(tr("Recover autosave"), this);
    auto *backupRadio   = new QRadioButton(tr("Recover from backup:"), this);
    auto *freshRadio    = new QRadioButton(tr("Start with an empty session"), this);

    auto *autosaveDetail = new QLabel(this);
    autosaveDetail->setWordWrap(true);
    autosaveDetail->setStyleSheet(QStringLiteral("color: #888; margin-left: 24px;"));
    if (QFile::exists(autosavePath)) {
        autosaveDetail->setText(tr("Last saved: %1").arg(formatSessionTimestamp(autosavePath)));
        autosaveRadio->setEnabled(true);
        autosaveRadio->setChecked(true);
    } else {
        autosaveDetail->setText(tr("No autosave file found."));
        autosaveRadio->setEnabled(false);
        backupRadio->setChecked(true);
        m_choice = Choice::RecoverBackup;
    }

    auto *backupCombo = new QComboBox(this);
    for (const QString &path : backupPaths) {
        const QFileInfo info(path);
        backupCombo->addItem(
            tr("%1  —  %2").arg(info.fileName(), formatSessionTimestamp(path)), path);
    }
    backupRadio->setEnabled(!backupPaths.isEmpty());
    backupCombo->setEnabled(!backupPaths.isEmpty());

    if (!autosaveRadio->isEnabled() && backupRadio->isEnabled())
        backupRadio->setChecked(true);
    if (!autosaveRadio->isEnabled() && !backupRadio->isEnabled()) {
        freshRadio->setChecked(true);
        m_choice = Choice::StartFresh;
    }

    auto *group = new QButtonGroup(this);
    group->addButton(autosaveRadio);
    group->addButton(backupRadio);
    group->addButton(freshRadio);

    connect(autosaveRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) m_choice = Choice::RecoverAutosave;
    });
    connect(backupRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) m_choice = Choice::RecoverBackup;
    });
    connect(freshRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) m_choice = Choice::StartFresh;
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, backupCombo]() {
        if (m_choice == Choice::RecoverBackup && backupCombo->count() > 0)
            m_selectedBackupPath = backupCombo->currentData().toString();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(autosaveRadio);
    layout->addWidget(autosaveDetail);
    layout->addWidget(backupRadio);
    layout->addWidget(backupCombo);
    layout->addWidget(freshRadio);
    layout->addStretch();
    layout->addWidget(buttons);
}
