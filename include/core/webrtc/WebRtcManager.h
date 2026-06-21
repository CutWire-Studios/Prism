#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <cstdint>

struct WebRtcPairingInfo {
    QString host;
    quint16 sigPort  = 0;
    quint16 httpPort = 0;
    QString token;
    QString relayUrl; ///< Non-empty when using a public relay (wss://…/ws).

    bool usesRelay() const { return !relayUrl.isEmpty(); }
};

/// Singleton owning the WebRTC signaling server and live peer sessions.
class WebRtcManager : public QObject {
    Q_OBJECT

public:
    static WebRtcManager &instance();

    static bool isAvailable();

    /// Lazily starts servers on bindAddress and returns pairing details for a new session.
    /// Pass relayUrl for public signaling (desktop connects outbound; no local WS server).
    WebRtcPairingInfo createSession(const QString &bindAddress, const QString &relayUrl = {});

    /// Starts servers if needed and returns pairing details for an existing or new session token.
    WebRtcPairingInfo ensureSession(const QString &bindAddress, const QString &token = {},
                                    const QString &relayUrl = {});

    QString bindAddress() const;
    bool hasSession(const QString &token) const;

    quint16 httpPort() const { return m_httpPort; }
    void destroySession(const QString &token);

    void registerViewer(const QString &token);
    void unregisterViewer(const QString &token);

    bool isPeerConnected(const QString &token) const;
    bool copyLatestFrame(const QString &token, QImage &out, uint64_t &seq, uint64_t sinceSeq) const;

    quint16 signalingPort() const { return m_sigPort; }

signals:
    void peerConnected(const QString &token);
    void peerDisconnected(const QString &token);

private:
    explicit WebRtcManager(QObject *parent = nullptr);
    ~WebRtcManager() override;

    WebRtcManager(const WebRtcManager &) = delete;
    WebRtcManager &operator=(const WebRtcManager &) = delete;

    class Impl;
    Impl *m_impl = nullptr;
    quint16 m_sigPort = 0;
    quint16 m_httpPort = 0;
};
