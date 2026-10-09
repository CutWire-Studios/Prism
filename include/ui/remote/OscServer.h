#pragma once

#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/remote/OscProtocol.h"

class QTimer;
class QUdpSocket;
class MainWindow;

/// UDP OSC server for show-control software (e.g. Linux Show Player). Settings
/// live in QSettings under osc/*; when a feedback host is set, state changes are
/// pushed there as OSC messages.
class OscServer : public QObject {
    Q_OBJECT
public:
    static constexpr quint16 kDefaultPort = 9000;
    static constexpr quint16 kDefaultFeedbackPort = 9001;

    explicit OscServer(MainWindow *mainWindow, QObject *parent = nullptr);
    ~OscServer() override;

    /// (Re)reads osc/* settings and starts, restarts or stops the server.
    /// Returns false if enabled but the port could not be bound.
    bool applySettings();
    void stop();

    bool isRunning() const { return m_socket != nullptr; }
    quint16 port() const { return m_port; }

private slots:
    void onReadyRead();
    void pollState();

private:
    struct Deck {
        NodeId  input = ~NodeId(0); // sentinel so the first poll reports everything
        int     slot = 0;
        QString name;
        bool    playing = false;
    };
    struct State {
        int     fader = -1;
        Deck    a, b;
        int     mode = -1;
        double  duration = -1.0;
        QString panic;
    };

    void dispatch(const OscProtocol::OscMessage &msg);
    void handleDeck(bool deckA, const QString &cmd, const QVariantList &args);
    void sendFullState();
    void sendDeckDiff(const char *deck, const Deck &now, const Deck &before);
    void send(const QString &address, const QVariantList &args = {});
    State captureState() const;

    QPointer<MainWindow> m_mainWindow;
    QUdpSocket *m_socket = nullptr;
    QTimer *m_pollTimer = nullptr;
    quint16 m_port = kDefaultPort;
    QHostAddress m_feedbackHost;
    quint16 m_feedbackPort = kDefaultFeedbackPort;
    State m_state;
};
