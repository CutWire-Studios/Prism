#pragma once

#include <QHash>
#include <QHostAddress>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include "core/Deck.h"
#include "app/remote/OscProtocol.h"

class QTimer;
class QUdpSocket;
class AppController;

/// UDP OSC server for show-control software (e.g. Linux Show Player). Settings
/// live in QSettings under osc/*; when a feedback host is set, state changes are
/// pushed there as OSC messages.
class OscServer : public QObject {
    Q_OBJECT
public:
    static constexpr quint16 kDefaultPort = 9000;
    static constexpr quint16 kDefaultFeedbackPort = 9001;

    explicit OscServer(AppController *mainWindow, QObject *parent = nullptr);
    ~OscServer() override;

    /// (Re)reads osc/* settings and starts, restarts or stops the server.
    /// Returns false if enabled but the port could not be bound.
    bool applySettings();
    void stop();

    bool isRunning() const { return m_socket != nullptr; }
    quint16 port() const { return m_port; }

    /// Control input: any OSC address outside /prism/ keeps its latest numeric value (first
    /// numeric or bool argument). Safe to read from any thread. 0 when never received.
    double controlValue(const QString &address) const;
    /// Received control addresses as [{address, value}], most recently updated first.
    QVariantList recentControls() const;

    /// Waits for the next control message (never a /prism/ show-control one) and emits learned()
    /// with its address, or learnTimedOut() after @p timeoutMs. Replaces a pending learn.
    void startLearn(int timeoutMs = kLearnTimeoutMs);
    void cancelLearn();
    bool isLearning() const { return m_learning; }

    static constexpr int kLearnTimeoutMs = 10000;

signals:
    void learned(const QString &address);
    void learnTimedOut();

private slots:
    void onReadyRead();
    void pollState();

private:
    struct DeckState {
        QString input = QStringLiteral("?"); // sentinel so the first poll reports everything
        int     slot = 0;
        QString name;
        bool    playing = false;
    };
    struct State {
        int     fader = -1;
        DeckState live, staging;
        int     mode = -1;
        double  duration = -1.0;
        QString panic;
    };

    void dispatch(const OscProtocol::OscMessage &msg);
    void recordControl(const OscProtocol::OscMessage &msg);
    void handleDeck(Deck deck, const QString &cmd, const QVariantList &args);
    QString sceneIdFromArg(const QVariant &arg) const;
    void handleStage(const QVariantList &args);
    void sendFullState();
    void sendDeckDiff(const char *deck, const DeckState &now, const DeckState &before);
    void send(const QString &address, const QVariantList &args = {});
    State captureState() const;

    QPointer<AppController> m_mainWindow;
    QUdpSocket *m_socket = nullptr;
    QTimer *m_pollTimer = nullptr;
    quint16 m_port = kDefaultPort;
    QHostAddress m_feedbackHost;
    quint16 m_feedbackPort = kDefaultFeedbackPort;
    State m_state;

    struct Control {
        double value = 0.0;
        quint64 seq = 0;
    };
    mutable QMutex m_controlMutex;
    QHash<QString, Control> m_controls;
    quint64 m_controlSeq = 0;
    QTimer *m_learnTimer = nullptr;
    bool m_learning = false;
};
