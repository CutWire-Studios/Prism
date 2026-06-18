#include "core/WebRtcManager.h"
#include "core/NetworkUtils.h"
#include "core/WebRtcPairing.h"
#include "core/WebRtcCamPage.h"
#include "core/FirewallUtils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>

#ifdef SWITCHX_HAVE_WEBRTC
#include <rtc/rtc.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#endif

namespace {

#ifdef SWITCHX_HAVE_WEBRTC
QString makeToken() {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    QString token;
    token.reserve(16);
    for (int i = 0; i < 16; ++i)
        token += QChar(chars[QRandomGenerator::global()->bounded(int(sizeof(chars) - 1))]);
    return token;
}

class H264Decoder {
public:
    H264Decoder() = default;
    ~H264Decoder() { reset(); }

    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;

    bool ensureOpen() {
        if (m_ctx) return true;
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!codec) return false;

        m_ctx = avcodec_alloc_context3(codec);
        if (!m_ctx) return false;
        m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        m_ctx->thread_count = 1;

        if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
            reset();
            return false;
        }

        m_frame = av_frame_alloc();
        m_pkt   = av_packet_alloc();
        return m_frame && m_pkt;
    }

    QImage decode(const uint8_t *data, int size) {
        if (!ensureOpen() || !data || size <= 0) return {};

        av_packet_unref(m_pkt);
        if (av_new_packet(m_pkt, size) < 0) return {};
        memcpy(m_pkt->data, data, static_cast<size_t>(size));

        if (avcodec_send_packet(m_ctx, m_pkt) < 0)
            return {};

        QImage out;
        while (avcodec_receive_frame(m_ctx, m_frame) == 0) {
            out = frameToRgb(m_frame);
            if (!out.isNull()) break;
        }
        return out;
    }

    void reset() {
        if (m_sws) {
            sws_freeContext(m_sws);
            m_sws = nullptr;
        }
        if (m_pkt) {
            av_packet_free(&m_pkt);
            m_pkt = nullptr;
        }
        if (m_frame) {
            av_frame_free(&m_frame);
            m_frame = nullptr;
        }
        if (m_ctx) {
            avcodec_free_context(&m_ctx);
            m_ctx = nullptr;
        }
        m_swsW = m_swsH = 0;
    }

private:
    QImage frameToRgb(AVFrame *frame) {
        if (!frame || frame->width <= 0 || frame->height <= 0) return {};

        if (!m_sws || m_swsW != frame->width || m_swsH != frame->height) {
            if (m_sws) sws_freeContext(m_sws);
            m_sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                   frame->width, frame->height, AV_PIX_FMT_RGB24,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
            m_swsW = frame->width;
            m_swsH = frame->height;
        }
        if (!m_sws) return {};

        QImage img(frame->width, frame->height, QImage::Format_RGB888);
        uint8_t *dstData[4]     = { img.bits(), nullptr, nullptr, nullptr };
        int      dstLinesize[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
        sws_scale(m_sws, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);
        return img;
    }

    AVCodecContext *m_ctx   = nullptr;
    AVFrame        *m_frame = nullptr;
    AVPacket       *m_pkt   = nullptr;
    SwsContext     *m_sws   = nullptr;
    int             m_swsW  = 0;
    int             m_swsH  = 0;
};

struct FrameBuffer {
    QImage   image;
    uint64_t seq = 0;
    std::mutex mutex;
};

struct Session {
    QString token;
    QPointer<QWebSocket> socket;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> videoTrack;
    std::shared_ptr<rtc::H264RtpDepacketizer> depacketizer;
    std::shared_ptr<rtc::RtcpReceivingSession> rtcpSession;
    H264Decoder decoder;
    FrameBuffer frames;
    bool peerConnected = false;
    int  viewerCount   = 0;
};
#endif // SWITCHX_HAVE_WEBRTC

} // namespace

#ifdef SWITCHX_HAVE_WEBRTC
class WebRtcManager::Impl {
public:
    explicit Impl(WebRtcManager *owner)
        : m_owner(owner)
    {
        rtc::InitLogger(rtc::LogLevel::Warning);
    }

    ~Impl() {
        if (m_httpServer) {
            m_httpServer->close();
            delete m_httpServer;
            m_httpServer = nullptr;
        }
        if (m_server) {
            m_server->close();
            delete m_server;
            m_server = nullptr;
        }
        m_sessions.clear();
    }

    bool ensureSigServer(const QHostAddress &bindAddress, quint16 preferredPort, quint16 &boundPort) {
        if (m_server && m_server->isListening() && m_bindAddress == bindAddress.toString()) {
            boundPort = m_server->serverPort();
            return true;
        }

        if (m_server) {
            m_server->close();
            delete m_server;
            m_server = nullptr;
        }

        m_server = new QWebSocketServer(QStringLiteral("SwitchX-WebRTC"),
                                        QWebSocketServer::NonSecureMode);
        if (!m_server->listen(bindAddress, preferredPort)) {
            delete m_server;
            m_server = nullptr;
            return false;
        }

        m_bindAddress = bindAddress.toString();
        boundPort = m_server->serverPort();
        QObject::connect(m_server, &QWebSocketServer::newConnection, m_owner, [this]() {
            onNewConnection();
        });
        return true;
    }

    bool ensureHttpServer(const QHostAddress &bindAddress, quint16 preferredPort, quint16 &boundPort) {
        if (m_httpServer && m_httpServer->isListening() && m_bindAddress == bindAddress.toString()) {
            boundPort = m_httpServer->serverPort();
            return true;
        }

        if (m_httpServer) {
            m_httpServer->close();
            delete m_httpServer;
            m_httpServer = nullptr;
        }

        m_httpServer = new QTcpServer();
        if (!m_httpServer->listen(bindAddress, preferredPort)) {
            delete m_httpServer;
            m_httpServer = nullptr;
            return false;
        }

        m_bindAddress = bindAddress.toString();
        boundPort = static_cast<quint16>(m_httpServer->serverPort());
        QObject::connect(m_httpServer, &QTcpServer::newConnection, m_owner, [this]() {
            onHttpConnection();
        });
        return true;
    }

    void onHttpConnection() {
        while (QTcpSocket *socket = m_httpServer->nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, m_owner, [this, socket]() {
                handleHttpRequest(socket);
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        }
    }

    void handleHttpRequest(QTcpSocket *socket) {
        const QByteArray requestData = socket->readAll();
        const QString request = QString::fromUtf8(requestData);
        const QStringList lines = request.split(QStringLiteral("\r\n"));
        if (lines.isEmpty()) return;

        const QStringList parts = lines.first().split(QLatin1Char(' '));
        if (parts.size() < 2) return;

        const QString method = parts[0];
        const QString path   = parts[1];

        if (method != QStringLiteral("GET")) {
            sendHttpText(socket, QStringLiteral("405 Method Not Allowed"), 405);
            return;
        }

        if (!path.startsWith(QStringLiteral("/cam"))) {
            sendHttpText(socket, QStringLiteral("404 Not Found"), 404);
            return;
        }

        QUrl url(QStringLiteral("http://local") + path);
        QUrlQuery query(url.query());
        QString token;
        quint16 sigPort = 0;
        if (!WebRtcPairing::decodeQuery(query, token, sigPort)) {
            sendHttpText(socket, QStringLiteral("Missing or invalid pairing data"), 400);
            return;
        }
        if (sigPort == 0 && m_server)
            sigPort = m_server->serverPort();

        const QByteArray body = WebRtcCamPage::html(token, sigPort).toUtf8();
        const QByteArray response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
            "Connection: close\r\n\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    static void sendHttpText(QTcpSocket *socket, const QString &text, int statusCode) {
        const QByteArray body = text.toUtf8();
        const QByteArray response = QByteArray("HTTP/1.1 ") + QByteArray::number(statusCode) + "\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
            "Connection: close\r\n\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    WebRtcPairingInfo createSession(const QString &bindAddress, quint16 sigPort, quint16 httpPort) {
        WebRtcPairingInfo info;
        if (bindAddress.isEmpty())
            return info;

        const QHostAddress bindHost(bindAddress);
        quint16 boundSig = 0;
        quint16 boundHttp = 0;
        if (!ensureSigServer(bindHost, sigPort, boundSig))
            return info;
        if (!ensureHttpServer(bindHost, httpPort, boundHttp))
            return info;

        auto session = std::make_shared<Session>();
        session->token = makeToken();
        {
            QMutexLocker lock(&m_mutex);
            m_sessions.emplace(session->token, session);
        }

        info.host    = bindAddress;
        info.sigPort = boundSig;
        info.token   = session->token;
        return info;
    }

    void destroySession(const QString &token) {
        QMutexLocker lock(&m_mutex);
        auto it = m_sessions.find(token);
        if (it == m_sessions.end()) return;
        const auto &session = it->second;
        if (session->peerConnected || session->viewerCount > 0) return;
        if (session->socket)
            session->socket->close();
        if (session->pc)
            session->pc->close();
        m_sessions.erase(it);
    }

    void registerViewer(const QString &token) {
        QMutexLocker lock(&m_mutex);
        auto it = m_sessions.find(token);
        if (it != m_sessions.end())
            ++it->second->viewerCount;
    }

    void unregisterViewer(const QString &token) {
        QMutexLocker lock(&m_mutex);
        auto it = m_sessions.find(token);
        if (it != m_sessions.end()) {
            --it->second->viewerCount;
            if (it->second->viewerCount < 0) it->second->viewerCount = 0;
        }
    }

    bool isPeerConnected(const QString &token) const {
        QMutexLocker lock(&m_mutex);
        auto it = m_sessions.find(token);
        return it != m_sessions.end() && it->second->peerConnected;
    }

    bool copyLatestFrame(const QString &token, QImage &out, uint64_t &seq, uint64_t sinceSeq) const {
        std::shared_ptr<Session> session;
        {
            QMutexLocker lock(&m_mutex);
            auto it = m_sessions.find(token);
            if (it == m_sessions.end()) return false;
            session = it->second;
        }

        std::lock_guard<std::mutex> frameLock(session->frames.mutex);
        if (session->frames.seq <= sinceSeq || session->frames.image.isNull())
            return false;
        out = session->frames.image;
        seq = session->frames.seq;
        return true;
    }

    void onNewConnection() {
        if (!m_server) return;
        QWebSocket *socket = m_server->nextPendingConnection();
        if (!socket) return;

        QObject::connect(socket, &QWebSocket::textMessageReceived, m_owner, [this, socket](const QString &msg) {
            handleMessage(socket, msg);
        });
        QObject::connect(socket, &QWebSocket::disconnected, m_owner, [this, socket]() {
            unbindSocket(socket);
            socket->deleteLater();
        });
    }

private:
    std::shared_ptr<Session> sessionForSocket(QWebSocket *socket) const {
        QMutexLocker lock(&m_mutex);
        const QString token = m_socketTokens.value(socket);
        if (token.isEmpty()) return {};
        auto it = m_sessions.find(token);
        if (it == m_sessions.end()) return {};
        return it->second;
    }

    void bindSocket(const QString &token, QWebSocket *socket) {
        QMutexLocker lock(&m_mutex);
        auto it = m_sessions.find(token);
        if (it == m_sessions.end()) return;
        if (it->second->socket && it->second->socket != socket)
            it->second->socket->close();
        it->second->socket = socket;
        m_socketTokens.insert(socket, token);
    }

    void unbindSocket(QWebSocket *socket) {
        QMutexLocker lock(&m_mutex);
        const QString token = m_socketTokens.take(socket);
        if (token.isEmpty()) return;
        auto it = m_sessions.find(token);
        if (it == m_sessions.end()) return;
        auto &session = it->second;
        if (session->socket == socket)
            session->socket = nullptr;
        if (session->peerConnected) {
            session->peerConnected = false;
            if (session->pc) {
                session->pc->close();
                session->pc.reset();
            }
            session->videoTrack.reset();
            session->depacketizer.reset();
            session->rtcpSession.reset();
            session->decoder.reset();
            QMetaObject::invokeMethod(m_owner, [this, token]() {
                emit m_owner->peerDisconnected(token);
            }, Qt::QueuedConnection);
        }
    }

    void handleMessage(QWebSocket *socket, const QString &raw) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (!doc.isObject()) return;
        const QJsonObject obj = doc.object();
        const QString type = obj.value(QStringLiteral("type")).toString();

        if (type == QStringLiteral("hello")) {
            bindSocket(obj.value(QStringLiteral("token")).toString(), socket);
            return;
        }

        auto session = sessionForSocket(socket);
        if (!session) return;

        if (type == QStringLiteral("offer")) {
            handleOffer(session, socket, obj.value(QStringLiteral("sdp")).toString());
        } else if (type == QStringLiteral("candidate")) {
            if (!session->pc) return;
            const QString cand = obj.value(QStringLiteral("candidate")).toString();
            const QString mid  = obj.value(QStringLiteral("mid")).toString();
            if (cand.isEmpty()) return;
            try {
                session->pc->addRemoteCandidate(rtc::Candidate(cand.toStdString(), mid.toStdString()));
            } catch (const std::exception &e) {
                qWarning() << "WebRTC addRemoteCandidate:" << e.what();
            }
        }
    }

    void handleOffer(const std::shared_ptr<Session> &session, QWebSocket *socket, const QString &sdp) {
        if (sdp.isEmpty()) return;

        if (session->pc)
            session->pc->close();

        try {
            rtc::Configuration config;
            config.disableAutoNegotiation = true;

            session->pc = std::make_shared<rtc::PeerConnection>(config);
            const QString token = session->token;

            session->pc->onStateChange([this, session, token](rtc::PeerConnection::State state) {
                if (state == rtc::PeerConnection::State::Connected) {
                    session->peerConnected = true;
                    QMetaObject::invokeMethod(m_owner, [this, token]() {
                        emit m_owner->peerConnected(token);
                    }, Qt::QueuedConnection);
                } else if (state == rtc::PeerConnection::State::Disconnected
                        || state == rtc::PeerConnection::State::Failed
                        || state == rtc::PeerConnection::State::Closed) {
                    if (session->peerConnected) {
                        session->peerConnected = false;
                        QMetaObject::invokeMethod(m_owner, [this, token]() {
                            emit m_owner->peerDisconnected(token);
                        }, Qt::QueuedConnection);
                    }
                }
            });

            session->pc->onTrack([this, session](std::shared_ptr<rtc::Track> track) {
                setupVideoTrack(session, std::move(track));
            });

            session->pc->onLocalCandidate([socket](rtc::Candidate candidate) {
                QJsonObject obj;
                obj.insert(QStringLiteral("type"), QStringLiteral("candidate"));
                obj.insert(QStringLiteral("candidate"), QString::fromStdString(std::string(candidate)));
                obj.insert(QStringLiteral("mid"), QString::fromStdString(candidate.mid()));
                const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
                QMetaObject::invokeMethod(socket, [socket, payload]() {
                    if (socket->isValid())
                        socket->sendTextMessage(QString::fromUtf8(payload));
                }, Qt::QueuedConnection);
            });

            session->pc->setRemoteDescription(rtc::Description(sdp.toStdString(), rtc::Description::Type::Offer));
            session->pc->setLocalDescription();

            if (auto local = session->pc->localDescription()) {
                QJsonObject answer;
                answer.insert(QStringLiteral("type"), QStringLiteral("answer"));
                answer.insert(QStringLiteral("sdp"), QString::fromStdString(std::string(local.value())));
                socket->sendTextMessage(QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact)));
            }
        } catch (const std::exception &e) {
            qWarning() << "WebRTC handleOffer:" << e.what();
        }
    }

    void setupVideoTrack(const std::shared_ptr<Session> &session, std::shared_ptr<rtc::Track> track) {
        if (!track || track->description().type() != "video")
            return;

        session->videoTrack = track;
        session->depacketizer = std::make_shared<rtc::H264RtpDepacketizer>();
        session->rtcpSession  = std::make_shared<rtc::RtcpReceivingSession>();
        session->depacketizer->addToChain(session->rtcpSession);
        track->setMediaHandler(session->depacketizer);

        track->onFrame([session](rtc::binary data, rtc::FrameInfo /*info*/) {
            if (data.empty()) return;
            QImage img = session->decoder.decode(reinterpret_cast<const uint8_t *>(data.data()),
                                                 static_cast<int>(data.size()));
            if (img.isNull()) return;
            {
                std::lock_guard<std::mutex> lock(session->frames.mutex);
                session->frames.image = std::move(img);
                ++session->frames.seq;
            }
        });
    }

    WebRtcManager *m_owner = nullptr;
    QWebSocketServer *m_server = nullptr;
    QTcpServer       *m_httpServer = nullptr;
    QString           m_bindAddress;
    mutable QMutex m_mutex;
    std::unordered_map<QString, std::shared_ptr<Session>> m_sessions;
    QHash<QWebSocket *, QString> m_socketTokens;
};
#endif // SWITCHX_HAVE_WEBRTC

WebRtcManager &WebRtcManager::instance() {
    static WebRtcManager mgr;
    return mgr;
}

WebRtcManager::WebRtcManager(QObject *parent)
    : QObject(parent)
{
#ifdef SWITCHX_HAVE_WEBRTC
    m_impl = new Impl(this);
    m_sigPort  = WebRtcPairing::kDefaultSigPort;
    m_httpPort = WebRtcPairing::kDefaultHttpPort;
#endif
}

WebRtcManager::~WebRtcManager() {
#ifdef SWITCHX_HAVE_WEBRTC
    FirewallUtils::releasePorts({
        WebRtcPairing::kDefaultSigPort,
        WebRtcPairing::kDefaultHttpPort
    });
    delete m_impl;
    m_impl = nullptr;
#endif
}

bool WebRtcManager::isAvailable() {
#ifdef SWITCHX_HAVE_WEBRTC
    return true;
#else
    return false;
#endif
}

WebRtcPairingInfo WebRtcManager::createSession(const QString &bindAddress) {
    WebRtcPairingInfo info;
#ifdef SWITCHX_HAVE_WEBRTC
    if (!m_impl) return info;
    info = m_impl->createSession(bindAddress, m_sigPort, m_httpPort);
#else
    Q_UNUSED(bindAddress);
#endif
    return info;
}

void WebRtcManager::destroySession(const QString &token) {
#ifdef SWITCHX_HAVE_WEBRTC
    if (m_impl) m_impl->destroySession(token);
#else
    Q_UNUSED(token);
#endif
}

void WebRtcManager::registerViewer(const QString &token) {
#ifdef SWITCHX_HAVE_WEBRTC
    if (m_impl) m_impl->registerViewer(token);
#else
    Q_UNUSED(token);
#endif
}

void WebRtcManager::unregisterViewer(const QString &token) {
#ifdef SWITCHX_HAVE_WEBRTC
    if (m_impl) m_impl->unregisterViewer(token);
#else
    Q_UNUSED(token);
#endif
}

bool WebRtcManager::isPeerConnected(const QString &token) const {
#ifdef SWITCHX_HAVE_WEBRTC
    return m_impl && m_impl->isPeerConnected(token);
#else
    Q_UNUSED(token);
    return false;
#endif
}

bool WebRtcManager::copyLatestFrame(const QString &token, QImage &out, uint64_t &seq, uint64_t sinceSeq) const {
#ifdef SWITCHX_HAVE_WEBRTC
    return m_impl && m_impl->copyLatestFrame(token, out, seq, sinceSeq);
#else
    Q_UNUSED(token);
    Q_UNUSED(out);
    Q_UNUSED(seq);
    Q_UNUSED(sinceSeq);
    return false;
#endif
}
