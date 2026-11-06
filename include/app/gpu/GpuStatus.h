#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Tells the shell when hardware acceleration was asked for and did not work, so it can offer to
// turn it off. The media code (VideoDecoder, ProgramRecorder) reports through
// prism::hwaccel::reportFailure(), which lives in core and knows nothing of QML; this object
// listens, marshals the report onto the GUI thread and raises accelerationFailed() once per
// (stage, backend) per session so a bad driver does not stack a dialog per clip.
//
// Exposed to QML as the GpuStatus singleton. Create it early — referencing it from a Connections
// in the root window is enough — or reports made before that are only delivered once something
// first asks for it (the constructor replays the latest one).
class GpuStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // The most recent report, "decode" or "encode"; empty until something fails.
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(QString backend READ backend NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)

public:
    explicit GpuStatus(QObject *parent = nullptr);
    ~GpuStatus() override;

    QString stage() const { return m_stage; }
    QString backend() const { return m_backend; }
    QString detail() const { return m_detail; }

signals:
    void changed();
    // stage is "decode" or "encode"; backend is a display name; detail is raw driver output.
    void accelerationFailed(const QString &stage, const QString &backend, const QString &detail);

private:
    void pull();

    QString m_stage;
    QString m_backend;
    QString m_detail;
    quint64 m_serial = 0;
    QSet<QString> m_reported;
};
