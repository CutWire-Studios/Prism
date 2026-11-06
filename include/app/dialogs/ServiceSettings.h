#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

/// OSC server preferences (QSettings osc/*). Edit the properties, then apply().
class OscSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)
    Q_PROPERTY(QString feedbackHost READ feedbackHost WRITE setFeedbackHost NOTIFY changed)
    Q_PROPERTY(int feedbackPort READ feedbackPort WRITE setFeedbackPort NOTIFY changed)

public:
    explicit OscSettings(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    int port() const { return m_port; }
    void setPort(int port);
    QString feedbackHost() const { return m_feedbackHost; }
    void setFeedbackHost(const QString &host);
    int feedbackPort() const { return m_feedbackPort; }
    void setFeedbackPort(int port);

    /// Discards unsaved edits and re-reads the stored values.
    Q_INVOKABLE void load();
    /// Stores the values and restarts the server accordingly.
    Q_INVOKABLE void apply();

signals:
    void changed();

private:
    bool m_enabled = false;
    int m_port = 0;
    QString m_feedbackHost;
    int m_feedbackPort = 0;
};

/// Agent (MCP) access: switch, endpoint and the copy-to-clipboard snippets.
class McpAccess : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString url READ url NOTIFY changed)
    Q_PROPERTY(QString token READ token NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit McpAccess(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool on);
    QString url() const;
    QString token() const;
    QString error() const;

    Q_INVOKABLE void copyCursor() const;
    Q_INVOKABLE void copyClaude() const;
    Q_INVOKABLE void copyStdio() const;
    Q_INVOKABLE void copyGuide() const;

signals:
    void changed();
};

/// OBS WebSocket connection settings and state.
class ObsConnection : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool connecting READ connecting NOTIFY changed)
    Q_PROPERTY(QString host READ host NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(QString password READ password NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit ObsConnection(QObject *parent = nullptr);

    bool available() const;
    bool connected() const;
    bool connecting() const { return m_connecting; }
    QString host() const;
    int port() const;
    QString password() const;
    QString error() const { return m_error; }

    Q_INVOKABLE void connectToObs(const QString &host, int port, const QString &password);
    Q_INVOKABLE void disconnectFromObs();

signals:
    void changed();

private:
    bool m_connecting = false;
    QString m_error;
};
