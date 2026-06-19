#include "core/WebRtcManager.h"
#include "core/NetworkUtils.h"
#include "core/WebRtcPairing.h"
#include "core/WebRtcCamPage.h"
#include "core/WebRtcTlsStore.h"
#include "core/WebRtcH264Sdp.h"
#include "core/SwitchXH264RtpDepacketizer.h"
#include "core/FirewallUtils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>
#include <QSslServer>
#include <QSslSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>

#ifdef SWITCHX_HAVE_WEBRTC
#include <rtc/rtc.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
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

    bool hasExtradata() const { return !m_avcExtradata.isEmpty(); }

    bool tryConfigureFromAnnexB(const uint8_t *data, int size) {
        if (hasExtradata() || !data || size < 5)
            return false;

        QByteArray sps;
        QByteArray pps;

        auto appendNal = [](QByteArray &dest, const uint8_t *nal, int nalSize) {
            if (nalSize <= 0)
                return;
            if (!dest.isEmpty())
                return;
            dest = QByteArray(reinterpret_cast<const char *>(nal), nalSize);
        };

        for (int i = 0; i + 4 < size;) {
            int startCode = 0;
            if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
                startCode = 3;
            } else if (i + 4 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0
                       && data[i + 3] == 1) {
                startCode = 4;
            } else {
                ++i;
                continue;
            }

            const int nalStart = i + startCode;
            if (nalStart >= size)
                break;

            int next = size;
            for (int j = nalStart + 1; j + 3 < size; ++j) {
                if (data[j] == 0 && data[j + 1] == 0
                    && (data[j + 2] == 1 || (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1))) {
                    next = j;
                    break;
                }
            }

            const int nalSize = next - nalStart;
            const uint8_t nalType = data[nalStart] & 0x1F;
            if (nalType == 7)
                appendNal(sps, data + nalStart, nalSize);
            else if (nalType == 8)
                appendNal(pps, data + nalStart, nalSize);

            i = next;
        }

        if (sps.isEmpty() || pps.isEmpty())
            return false;

        const QByteArray avc = WebRtcH264Sdp::buildAvcExtradata(sps, pps);
        if (avc.isEmpty())
            return false;

        qInfo() << "WebRTC: configured H.264 decoder from in-band SPS/PPS";
        return setAvcExtradata(avc);
    }

    bool setAvcExtradata(const QByteArray &avcExtradata) {
        m_avcExtradata = avcExtradata;
        reset();
        return !avcExtradata.isEmpty() && ensureOpen();
    }

    bool ensureOpen() {
        if (m_ctx) return true;
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!codec) return false;

        m_ctx = avcodec_alloc_context3(codec);
        if (!m_ctx) return false;
        m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        m_ctx->thread_count = 1;
        m_ctx->has_b_frames = 0;

        if (!m_avcExtradata.isEmpty()) {
            m_ctx->extradata_size = m_avcExtradata.size();
            m_ctx->extradata = static_cast<uint8_t *>(
                av_malloc(static_cast<size_t>(m_ctx->extradata_size) + AV_INPUT_BUFFER_PADDING_SIZE));
            if (!m_ctx->extradata) {
                reset();
                return false;
            }
            memcpy(m_ctx->extradata, m_avcExtradata.constData(),
                   static_cast<size_t>(m_ctx->extradata_size));
            memset(m_ctx->extradata + m_ctx->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        }

        if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
            reset();
            return false;
        }

        m_parser = av_parser_init(AV_CODEC_ID_H264);
        m_frame  = av_frame_alloc();
        m_pkt    = av_packet_alloc();
        return m_parser && m_frame && m_pkt;
    }

    QImage decode(const uint8_t *data, int size) {
        if (!data || size <= 0) return {};
        tryConfigureFromAnnexB(data, size);
        if (!ensureOpen()) return {};

        QImage out;
        const uint8_t *cursor   = data;
        int            remaining = size;

        while (remaining > 0) {
            uint8_t *parsed     = nullptr;
            int      parsedSize = 0;
            const int consumed = av_parser_parse2(
                m_parser, m_ctx, &parsed, &parsedSize, cursor, remaining,
                AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (consumed < 0)
                break;

            cursor += consumed;
            remaining -= consumed;

            if (parsedSize <= 0)
                continue;

            av_packet_unref(m_pkt);
            if (av_new_packet(m_pkt, parsedSize) < 0)
                continue;
            memcpy(m_pkt->data, parsed, static_cast<size_t>(parsedSize));

            if (avcodec_send_packet(m_ctx, m_pkt) < 0)
                continue;

            while (avcodec_receive_frame(m_ctx, m_frame) == 0) {
                out = frameToRgb(m_frame);
                if (!out.isNull())
                    return out;
            }
        }

        return out;
    }

    void reset() {
        if (m_parser) {
            av_parser_close(m_parser);
            m_parser = nullptr;
        }
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

    AVCodecContext       *m_ctx    = nullptr;
    AVCodecParserContext *m_parser = nullptr;
    AVFrame              *m_frame  = nullptr;
    AVPacket             *m_pkt    = nullptr;
    SwsContext     *m_sws   = nullptr;
    int             m_swsW  = 0;
    int             m_swsH  = 0;
    QByteArray      m_avcExtradata;
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
    std::shared_ptr<SwitchXH264RtpDepacketizer> depacketizer;
    std::shared_ptr<rtc::RtcpReceivingSession> rtcpSession;
    H264Decoder decoder;
    FrameBuffer frames;
    bool peerConnected = false;
    int  viewerCount   = 0;
    bool keyframeBootstrapDone = false;
    bool loggedFirstRtpFrame   = false;
    bool loggedDecodeFailure   = false;
};
#endif // SWITCHX_HAVE_WEBRTC

} // namespace

#ifdef SWITCHX_HAVE_WEBRTC
class WebRtcManager::Impl {
public:
    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;

    explicit Impl(WebRtcManager *owner)
        : m_owner(owner)
    {
        rtc::InitLogger(rtc::LogLevel::Warning);
    }

    ~Impl() {
        if (m_httpsServer) {
            m_httpsServer->close();
            delete m_httpsServer;
            m_httpsServer = nullptr;
        }
        if (m_wssBridge) {
            delete m_wssBridge;
            m_wssBridge = nullptr;
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

    bool ensureHttpsServer(const QHostAddress &bindAddress, quint16 preferredPort, quint16 &boundPort) {
        if (m_httpsServer && m_httpsServer->isListening() && m_bindAddress == bindAddress.toString()) {
            boundPort = static_cast<quint16>(m_httpsServer->serverPort());
            return true;
        }

        QString tlsError;
        if (!WebRtcTlsStore::ensureCertificate(bindAddress.toString(), &tlsError)) {
            qWarning() << "WebRTC TLS:" << tlsError;
            return false;
        }

        if (m_httpsServer) {
            m_httpsServer->close();
            delete m_httpsServer;
            m_httpsServer = nullptr;
        }
        if (m_wssBridge) {
            delete m_wssBridge;
            m_wssBridge = nullptr;
        }

        m_wssBridge = new QWebSocketServer(QStringLiteral("SwitchX-WebRTC-WSS"),
                                           QWebSocketServer::SecureMode, m_owner);
        m_wssBridge->setSslConfiguration(WebRtcTlsStore::sslConfiguration());
        QObject::connect(m_wssBridge, &QWebSocketServer::newConnection, m_owner, [this]() {
            onNewConnection();
        });

        m_httpsServer = new QSslServer(m_owner);
        m_httpsServer->setSslConfiguration(WebRtcTlsStore::sslConfiguration());
        if (!m_httpsServer->listen(bindAddress, preferredPort)) {
            delete m_httpsServer;
            m_httpsServer = nullptr;
            delete m_wssBridge;
            m_wssBridge = nullptr;
            return false;
        }

        m_bindAddress = bindAddress.toString();
        boundPort = static_cast<quint16>(m_httpsServer->serverPort());
        QObject::connect(m_httpsServer, &QSslServer::startedEncryptionHandshake, m_owner,
                         [this](QSslSocket *socket) {
                             if (!socket)
                                 return;
                             onTlsConnection(socket);
                         });
        return true;
    }

    void onTlsConnection(QSslSocket *socket) {
        if (!socket) return;
        QObject::connect(socket, &QSslSocket::disconnected, socket, &QSslSocket::deleteLater);
        QObject::connect(socket, &QSslSocket::readyRead, m_owner, [this, socket]() {
            handleTlsRequest(socket);
        });
    }

    void handleTlsRequest(QSslSocket *socket) {
        if (socket->property("switchxHandled").toBool())
            return;

        const QByteArray buffered = socket->peek(qMax<qint64>(socket->bytesAvailable(), 4096));
        const int headerEnd = buffered.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        const QByteArray headers = buffered.left(headerEnd);
        const QString headerText = QString::fromUtf8(headers);
        const QStringList lines = headerText.split(QStringLiteral("\r\n"));
        if (lines.isEmpty())
            return;

        const QStringList parts = lines.first().split(QLatin1Char(' '));
        if (parts.size() < 2)
            return;

        const QString method = parts[0];
        const QString path   = parts[1];

        const bool wantsWebSocket = headerText.contains(QStringLiteral("Upgrade:"), Qt::CaseInsensitive)
            && headerText.contains(QStringLiteral("websocket"), Qt::CaseInsensitive);
        if (wantsWebSocket && path.startsWith(QStringLiteral("/ws"))) {
            socket->setProperty("switchxHandled", true);
            if (m_wssBridge)
                m_wssBridge->handleConnection(socket);
            return;
        }

        socket->setProperty("switchxHandled", true);

        if (method != QStringLiteral("GET")) {
            sendHttpText(socket, QStringLiteral("405 Method Not Allowed"), 405);
            return;
        }

        if (!path.startsWith(QStringLiteral("/cam"))) {
            sendHttpText(socket, QStringLiteral("404 Not Found"), 404);
            return;
        }

        QUrl url(QStringLiteral("https://local") + path);
        QUrlQuery query(url.query());
        QString token;
        quint16 sigPort = 0;
        if (!WebRtcPairing::decodeQuery(query, token, sigPort)) {
            sendHttpText(socket, QStringLiteral("Missing or invalid pairing data"), 400);
            return;
        }
        Q_UNUSED(sigPort);

        const QByteArray body = WebRtcCamPage::html(token, 0).toUtf8();
        const QByteArray response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
            "Connection: close\r\n\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    static void sendHttpText(QSslSocket *socket, const QString &text, int statusCode) {
        const QByteArray body = text.toUtf8();
        const QByteArray response = QByteArray("HTTP/1.1 ") + QByteArray::number(statusCode) + "\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
            "Connection: close\r\n\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    WebRtcPairingInfo createSession(const QString &bindAddress, quint16 sigPort, quint16 httpPort,
                                    const QString &existingToken = {}) {
        WebRtcPairingInfo info;
        if (bindAddress.isEmpty())
            return info;

        const QHostAddress bindHost(bindAddress);
        quint16 boundSig = 0;
        quint16 boundHttp = 0;
        if (!ensureSigServer(bindHost, sigPort, boundSig))
            return info;
        if (!ensureHttpsServer(bindHost, httpPort, boundHttp))
            return info;

        if (!existingToken.isEmpty()) {
            QMutexLocker lock(&m_mutex);
            auto it = m_sessions.find(existingToken);
            if (it != m_sessions.end()) {
                info.host    = bindAddress;
                info.sigPort = boundSig;
                info.token   = existingToken;
                return info;
            }
        }

        auto session = std::make_shared<Session>();
        session->token = existingToken.isEmpty() ? makeToken() : existingToken;
        {
            QMutexLocker lock(&m_mutex);
            m_sessions.emplace(session->token, session);
        }

        info.host    = bindAddress;
        info.sigPort = boundSig;
        info.token   = session->token;
        return info;
    }

    QString bindAddress() const {
        QMutexLocker lock(&m_mutex);
        return m_bindAddress;
    }

    bool hasSession(const QString &token) const {
        QMutexLocker lock(&m_mutex);
        return m_sessions.find(token) != m_sessions.end();
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
        QWebSocket *socket = nullptr;
        if (m_server && m_server->hasPendingConnections())
            socket = m_server->nextPendingConnection();
        else if (m_wssBridge && m_wssBridge->hasPendingConnections())
            socket = m_wssBridge->nextPendingConnection();
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

        session->videoTrack = nullptr;
        session->peerConnected = false;
        session->keyframeBootstrapDone = false;
        session->loggedFirstRtpFrame = false;
        session->loggedDecodeFailure = false;
        {
            std::lock_guard<std::mutex> lock(session->frames.mutex);
            session->frames.image = {};
            session->frames.seq = 0;
        }

        try {
            rtc::Configuration config;
            config.disableAutoNegotiation = true;

            QByteArray sps;
            QByteArray pps;
            if (WebRtcH264Sdp::parseSpropParameterSets(sdp, &sps, &pps)) {
                const QByteArray avc = WebRtcH264Sdp::buildAvcExtradata(sps, pps);
                if (!avc.isEmpty() && session->decoder.setAvcExtradata(avc))
                    qInfo() << "WebRTC: configured H.264 decoder from SDP parameter sets";
                else
                    qWarning() << "WebRTC: could not configure H.264 decoder from SDP parameter sets";
            } else {
                qInfo() << "WebRTC: offer SDP missing sprop-parameter-sets; waiting for in-band SPS/PPS";
            }

            session->pc = std::make_shared<rtc::PeerConnection>(config);
            const QString token = session->token;

            session->pc->onStateChange([this, session, token](rtc::PeerConnection::State state) {
                if (state == rtc::PeerConnection::State::Connected) {
                    session->peerConnected = true;
                    bootstrapKeyframes(session);
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

    void bootstrapKeyframes(const std::shared_ptr<Session> &session) {
        if (!session || session->keyframeBootstrapDone)
            return;
        if (!session->videoTrack || !session->peerConnected)
            return;

        session->keyframeBootstrapDone = true;
        try {
            session->videoTrack->requestKeyframe();
            qInfo() << "WebRTC: requested keyframe";
        } catch (const std::exception &e) {
            qWarning() << "WebRTC requestKeyframe:" << e.what();
        }

        QTimer::singleShot(2000, m_owner, [session]() {
            if (!session || session->frames.seq != 0 || !session->videoTrack)
                return;
            try {
                session->videoTrack->requestKeyframe();
                qInfo() << "WebRTC: retrying keyframe request (no decoded frame yet)";
            } catch (const std::exception &e) {
                qWarning() << "WebRTC requestKeyframe retry:" << e.what();
            }
        });
    }

    void setupVideoTrack(const std::shared_ptr<Session> &session, std::shared_ptr<rtc::Track> track) {
        if (!track || track->description().type() != "video")
            return;

        session->videoTrack = track;
        session->depacketizer = std::make_shared<SwitchXH264RtpDepacketizer>();
        session->rtcpSession  = std::make_shared<rtc::RtcpReceivingSession>();
        session->depacketizer->addToChain(session->rtcpSession);
        track->setMediaHandler(session->depacketizer);
        bootstrapKeyframes(session);

        track->onFrame([session](rtc::binary data, rtc::FrameInfo /*info*/) {
            if (data.empty()) return;
            if (!session->loggedFirstRtpFrame) {
                session->loggedFirstRtpFrame = true;
                qInfo() << "WebRTC: first RTP frame received (" << data.size() << " bytes)";
            }
            QImage img = session->decoder.decode(reinterpret_cast<const uint8_t *>(data.data()),
                                                 static_cast<int>(data.size()));
            if (img.isNull()) {
                if (!session->loggedDecodeFailure) {
                    session->loggedDecodeFailure = true;
                    qWarning() << "WebRTC: first decode attempt produced no image";
                }
                return;
            }
            {
                std::lock_guard<std::mutex> lock(session->frames.mutex);
                session->frames.image = std::move(img);
                ++session->frames.seq;
            }
        });
    }

    WebRtcManager *m_owner = nullptr;
    QWebSocketServer *m_server = nullptr;
    QSslServer       *m_httpsServer = nullptr;
    QWebSocketServer *m_wssBridge = nullptr;
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
    return ensureSession(bindAddress);
}

WebRtcPairingInfo WebRtcManager::ensureSession(const QString &bindAddress, const QString &token) {
    WebRtcPairingInfo info;
#ifdef SWITCHX_HAVE_WEBRTC
    if (!m_impl) return info;
    info = m_impl->createSession(bindAddress, m_sigPort, m_httpPort, token);
#else
    Q_UNUSED(bindAddress);
    Q_UNUSED(token);
#endif
    return info;
}

QString WebRtcManager::bindAddress() const {
#ifdef SWITCHX_HAVE_WEBRTC
    if (m_impl) return m_impl->bindAddress();
#endif
    return {};
}

bool WebRtcManager::hasSession(const QString &token) const {
#ifdef SWITCHX_HAVE_WEBRTC
    return m_impl && m_impl->hasSession(token);
#else
    Q_UNUSED(token);
    return false;
#endif
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
