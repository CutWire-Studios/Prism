#include "ui/remote/RemoteControlServer.h"
#include "core/webrtc/FirewallUtils.h"
#include "ui/mainwindow/MainWindow.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/nodes/ClipNodeModel.h"
#include "ui/remote/RemoteProtocol.h"
#include "core/webrtc/WebRtcPairing.h"
#include "core/webrtc/WebRtcCamPage.h"
#ifdef PRISM_HAVE_WEBRTC
#include "core/webrtc/WebRtcManager.h"
#endif
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QSlider>
#include <QMetaObject>

RemoteControlServer::RemoteControlServer(MainWindow *mainWindow, TransitionController *transitionCtrl, QSlider *faderSlider, QObject *parent)
    : QTcpServer(parent)
    , m_mainWindow(mainWindow)
    , m_transitionCtrl(transitionCtrl)
    , m_faderSlider(faderSlider)
{
}

RemoteControlServer::~RemoteControlServer() {
    stopServer();
}

bool RemoteControlServer::startServer(const QHostAddress &address, quint16 port) {
    if (listen(address, port)) {
        m_isRunning = true;
        m_serverAddress = serverAddress();
        m_serverPort = serverPort();
        return true;
    }
    return false;
}

void RemoteControlServer::stopServer() {
    if (isListening()) {
        close();
    }
    m_isRunning = false;
    FirewallUtils::releasePorts({kPort});
}

void RemoteControlServer::incomingConnection(qintptr socketDescriptor) {
    QTcpSocket *socket = new QTcpSocket(this);
    if (socket->setSocketDescriptor(socketDescriptor)) {
        connect(socket, &QTcpSocket::readyRead, this, &RemoteControlServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &RemoteControlServer::onDisconnected);
    } else {
        delete socket;
    }
}

void RemoteControlServer::onReadyRead() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    const RemoteProtocol::RequestLine request =
        RemoteProtocol::parseRequestLine(QString::fromUtf8(socket->readAll()));
    if (!request.valid)
        return;

    switch (RemoteProtocol::matchRoute(request.method, request.path)) {
    case RemoteProtocol::Route::Root:
        sendHtmlResponse(socket);
        break;

    case RemoteProtocol::Route::Cam: {
        QUrl url(QStringLiteral("http://local") + request.path);
        QUrlQuery query(url.query());
        QString token;
        quint16 sigPort = 0;
        if (!WebRtcPairing::decodeQuery(query, token, sigPort)) {
            sendTextResponse(socket,
                QStringLiteral("Missing or invalid pairing data (?d=<base64> or ?s=<token>)"), 400);
        } else {
#ifdef PRISM_HAVE_WEBRTC
            if (sigPort == 0)
                sigPort = WebRtcManager::instance().signalingPort();
#endif
            sendCamHtmlResponse(socket, token, sigPort);
        }
        break;
    }

    case RemoteProtocol::Route::Status: {
        RemoteProtocol::StatusData status;
        status.fader = m_faderSlider ? m_faderSlider->value() : 0;

        if (m_mainWindow) {
            status.hasDeck = true;
            status.activeA = m_mainWindow->activeNodeA();
            status.activeB = m_mainWindow->activeNodeB();
            status.isPlayingA = m_mainWindow->isPlayingA();
            status.isPlayingB = m_mainWindow->isPlayingB();

            if (auto *editor = m_mainWindow->clipNodeEditor()) {
                for (auto *node : editor->allNodes()) {
                    if (node && node->hasSource()) {
                        RemoteProtocol::ClipInfo clip;
                        clip.id = node->nodeId();
                        clip.name = node->sourceName();
                        clip.activeA = (node->nodeId() == m_mainWindow->activeNodeA());
                        clip.activeB = (node->nodeId() == m_mainWindow->activeNodeB());
                        status.clips.append(clip);
                    }
                }
            }
        }

        if (m_transitionCtrl) {
            status.hasTransition = true;
            status.transitionMode = m_transitionCtrl->currentModeIndex();
            status.transitionDuration = m_transitionCtrl->currentDurationSecs();
            status.transitionModes = m_transitionCtrl->transitionModeNames();
        }

        sendJsonResponse(socket, RemoteProtocol::buildStatusJson(status));
        break;
    }

    case RemoteProtocol::Route::Fader: {
        int val = 0;
        if (RemoteProtocol::intParam(request.path, QStringLiteral("val"), val) && m_faderSlider)
            m_faderSlider->setValue(val);
        sendJsonResponse(socket, "{\"status\": \"ok\"}");
        break;
    }

    case RemoteProtocol::Route::Cut:
        if (m_transitionCtrl)
            m_transitionCtrl->onCutTransitionClicked();
        sendJsonResponse(socket, "{\"status\": \"ok\"}");
        break;

    case RemoteProtocol::Route::Auto:
        if (m_transitionCtrl)
            m_transitionCtrl->onAutoTransitionClicked();
        sendJsonResponse(socket, "{\"status\": \"ok\"}");
        break;

    case RemoteProtocol::Route::SelectA: {
        quint64 id = 0;
        if (RemoteProtocol::uint64Param(request.path, QStringLiteral("id"), id) && m_mainWindow)
            m_mainWindow->selectNodeA(id);
        sendJsonResponse(socket, "{\"status\": \"ok\"}");
        break;
    }

    case RemoteProtocol::Route::SelectB: {
        quint64 id = 0;
        if (RemoteProtocol::uint64Param(request.path, QStringLiteral("id"), id) && m_mainWindow)
            m_mainWindow->selectNodeB(id);
        sendJsonResponse(socket, "{\"status\": \"ok\"}");
        break;
    }

    case RemoteProtocol::Route::TogglePlayA:
        if (m_mainWindow)
            m_mainWindow->togglePlayA();
        sendJsonResponse(socket, "{\"status\": \"ok\"}");
        break;

    case RemoteProtocol::Route::TogglePlayB:
        if (m_mainWindow)
            m_mainWindow->togglePlayB();
        sendJsonResponse(socket, "{\"status\": \"ok\"}");
        break;

    case RemoteProtocol::Route::SetTransitionMode: {
        int index = 0;
        if (RemoteProtocol::intParam(request.path, QStringLiteral("index"), index) && m_transitionCtrl)
            m_transitionCtrl->setTransitionModeIndex(index);
        sendJsonResponse(socket, "{\"status\": \"ok\"}");
        break;
    }

    case RemoteProtocol::Route::SetDuration: {
        double val = 0.0;
        if (RemoteProtocol::doubleParam(request.path, QStringLiteral("val"), val) && m_transitionCtrl)
            m_transitionCtrl->setTransitionDuration(val);
        sendJsonResponse(socket, "{\"status\": \"ok\"}");
        break;
    }

    case RemoteProtocol::Route::NotFound:
        sendTextResponse(socket, "404 Not Found", 404);
        break;

    case RemoteProtocol::Route::MethodNotAllowed:
        sendTextResponse(socket, "405 Method Not Allowed", 405);
        break;
    }
}

void RemoteControlServer::onDisconnected() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        socket->deleteLater();
    }
}

void RemoteControlServer::sendHtmlResponse(QTcpSocket *socket) {
    const QByteArray body = getWebPageHtml().toUtf8();
    socket->write(RemoteProtocol::buildHttpResponse(
        200, "text/html; charset=utf-8", body));
}

void RemoteControlServer::sendCamHtmlResponse(QTcpSocket *socket, const QString &token, quint16 sigPort) {
    const QByteArray body = WebRtcCamPage::html(token, sigPort).toUtf8();
    socket->write(RemoteProtocol::buildHttpResponse(
        200, "text/html; charset=utf-8", body));
}

void RemoteControlServer::sendJsonResponse(QTcpSocket *socket, const QByteArray &json) {
    socket->write(RemoteProtocol::buildHttpResponse(
        200, "application/json", json));
}

void RemoteControlServer::sendTextResponse(QTcpSocket *socket, const QString &text, int statusCode) {
    const QByteArray body = text.toUtf8();
    socket->write(RemoteProtocol::buildHttpResponse(
        statusCode, "text/plain", body));
}

QString RemoteControlServer::getWebPageHtml() const {
    // MSVC limits raw string literals to 64 KiB; concatenate chunks.
    return QString(
    R"html(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>CutWire Prism Remote Console</title>
    <style>
        :root {
            --bg-color: #0d0d0f;
            --panel-bg: rgba(22, 22, 26, 0.7);
            --primary-gold: #e5a93b;
            --primary-gold-glow: rgba(229, 169, 59, 0.4);
            --deck-a-color: #ff3b30;
            --deck-a-glow: rgba(255, 59, 48, 0.3);
            --deck-b-color: #34c759;
            --deck-b-glow: rgba(52, 199, 89, 0.3);
            --text-color: #f5f5f7;
            --text-muted: #8e8e93;
            --border-color: rgba(255, 255, 255, 0.08);
            --btn-active: rgba(255, 255, 255, 0.12);
        }
        
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            user-select: none;
            -webkit-user-select: none;
        }

        body {
            background-color: var(--bg-color);
            background-image: radial-gradient(circle at top right, rgba(229, 169, 59, 0.08), transparent 40%),
                              radial-gradient(circle at bottom left, rgba(255, 59, 48, 0.05), transparent 45%);
            color: var(--text-color);
            font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "Segoe UI", Roboto, sans-serif;
            display: flex;
            justify-content: center;
            min-height: 100vh;
            padding: 16px;
            overflow-y: auto;
        }

        .wrapper {
            width: 100%;
            max-width: 480px;
            display: flex;
            flex-direction: column;
            gap: 16px;
        }

        .header {
            background: var(--panel-bg);
            border: 1px solid var(--border-color);
            border-radius: 20px;
            padding: 16px 20px;
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            display: flex;
            justify-content: space-between;
            align-items: center;
            box-shadow: 0 4px 30px rgba(0,0,0,0.3);
        }

        .brand h1 {
            font-size: 20px;
            font-weight: 700;
            letter-spacing: -0.5px;
            background: linear-gradient(135deg, #ffffff, var(--primary-gold));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .brand .subtitle {
            font-size: 11px;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-top: 2px;
        }

        .status-badge {
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 12px;
            font-weight: 600;
            color: var(--text-muted);
            background: rgba(255,255,255,0.05);
            padding: 6px 12px;
            border-radius: 20px;
            border: 1px solid var(--border-color);
        }

        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background-color: #ff3b30;
            box-shadow: 0 0 8px #ff3b30;
            transition: all 0.3s ease;
        }

        .status-dot.connected {
            background-color: #34c759;
            box-shadow: 0 0 8px #34c759;
        }

        .deck-monitors {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }

        .deck-card {
            background: var(--panel-bg);
            border: 1px solid var(--border-color);
            border-radius: 20px;
            padding: 16px;
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            display: flex;
            flex-direction: column;
            gap: 10px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.2);
            transition: border-color 0.3s ease;
        }

        .deck-card.deck-a {
            border-bottom: 3px solid var(--deck-a-color);
        }

        .deck-card.deck-b {
            border-bottom: 3px solid var(--deck-b-color);
        }

        .deck-label {
            font-size: 11px;
            font-weight: 800;
            letter-spacing: 1px;
            text-transform: uppercase;
        }

        .deck-a .deck-label { color: var(--deck-a-color); }
        .deck-b .deck-label { color: var(--deck-b-color); }

        .deck-clip-name {
            font-size: 15px;
            font-weight: 600;
            color: var(--text-color);
            min-height: 40px;
            display: -webkit-box;
            -webkit-line-clamp: 2;
            -webkit-box-orient: vertical;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .deck-controls {
            display: flex;
            align-items: center;
            justify-content: space-between;
        }

        .play-toggle {
            background: rgba(255,255,255,0.06);
            border: 1px solid var(--border-color);
            color: var(--text-color);
            border-radius: 12px;
            width: 40px;
            height: 40px;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            outline: none;
            transition: all 0.2s ease;
        }

        .play-toggle:active {
            transform: scale(0.92);
            background: var(--btn-active);
        }

        .play-toggle svg {
            width: 16px;
            height: 16px;
            fill: currentColor;
        }

        .play-toggle.playing {
            background: rgba(255, 255, 255, 0.15);
            color: var(--primary-gold);
            border-color: rgba(229, 169, 59, 0.3);
            box-shadow: 0 0 10px rgba(229, 169, 59, 0.2);
        }

        .main-panel {
            background: var(--panel-bg);
            border: 1px solid var(--border-color);
            border-radius: 24px;
            padding: 20px;
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            box-shadow: 0 12px 40px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.03);
            display: flex;
            flex-direction: column;
            gap: 20px;
        }

        .section-title {
            font-size: 12px;
            font-weight: 700;
            text-transform: uppercase;
            letter-spacing: 1.5px;
            color: var(--text-muted);
            margin-bottom: 4px;
        }

        .fader-container {
            text-align: center;
            padding: 10px 4px;
        }

        .fader-labels {
            display: flex;
            justify-content: space-between;
            font-size: 12px;
            font-weight: 700;
            color: var(--text-muted);
            margin-bottom: 8px;
        }

        .fader-slider-wrap {
            position: relative;
        }

        .fader-slider {
            -webkit-appearance: none;
            width: 100%;
            height: 10px;
            border-radius: 5px;
            background: rgba(255, 255, 255, 0.08);
            outline: none;
            margin: 15px 0;
            border: 1px solid rgba(255,255,255,0.04);
        }

        .fader-slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 38px;
            height: 38px;
            border-radius: 50%;
            background: radial-gradient(circle, #ffffff 20%, var(--primary-gold) 100%);
            cursor: pointer;
            box-shadow: 0 0 15px var(--primary-gold-glow), 0 6px 12px rgba(0,0,0,0.5);
            border: 2px solid #16161a;
            transition: transform 0.1s;
        }

        .fader-slider::-webkit-slider-thumb:active {
            transform: scale(1.15);
        }

        .action-buttons {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }

        .btn {
            background: linear-gradient(135deg, rgba(255,255,255,0.05), rgba(255,255,255,0.02));
            border: 1px solid var(--border-color);
            color: var(--text-color);
            border-radius: 16px;
            padding: 16px;
            font-size: 15px;
            font-weight: 700;
            letter-spacing: 0.5px;
            cursor: pointer;
            outline: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.2);
            transition: all 0.2s ease;
            -webkit-tap-highlight-color: transparent;
        }

        .btn:active {
            transform: scale(0.96);
            background: var(--btn-active);
        }

        .btn-cut {
            border-color: rgba(255, 59, 48, 0.25);
            background: linear-gradient(135deg, rgba(255, 59, 48, 0.12), rgba(255, 59, 48, 0.03));
            color: #ff453a;
        }

        .btn-cut:active {
            background: rgba(255, 59, 48, 0.22);
            border-color: rgba(255, 59, 48, 0.45);
        }

        .btn-auto {
            border-color: rgba(229, 169, 59, 0.25);
            background: linear-gradient(135deg, rgba(229, 169, 59, 0.12), rgba(229, 169, 59, 0.03));
            color: var(--primary-gold);
        }

        .btn-auto:active {
            background: rgba(229, 169, 59, 0.22);
            border-color: rgba(229, 169, 59, 0.45);
        }

        .transition-config {
            display: grid;
            grid-template-columns: 2fr 1fr;
            gap: 12px;
            background: rgba(0, 0, 0, 0.15);
            padding: 12px;
            border-radius: 16px;
            border: 1px solid var(--border-color);
        }

        .select-style, .input-style {
            background: rgba(255,255,255,0.06);
            border: 1px solid var(--border-color);
            color: var(--text-color);
            padding: 10px 12px;
            border-radius: 10px;
            font-size: 14px;
            outline: none;
            width: 100%;
        }

        .select-style option {
            background-color: #1e1e24;
            color: var(--text-color);
        }

        .clips-panel {
            background: var(--panel-bg);
            border: 1px solid var(--border-color);
            border-radius: 24px;
            padding: 20px;
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            box-shadow: 0 12px 40px rgba(0,0,0,0.4);
            display: flex;
            flex-direction: column;
            gap: 12px;
        }

        .clips-list {
            display: flex;
            flex-direction: column;
            gap: 8px;
            max-height: 280px;
            overflow-y: auto;
            padding-right: 4px;
        }

        .clips-list::-webkit-scrollbar {
            width: 6px;
        }
        .clips-list::-webkit-scrollbar-track {
            background: rgba(0, 0, 0, 0.1);
            border-radius: 3px;
        }
        .clips-list::-webkit-scrollbar-thumb {
            background: rgba(255, 255, 255, 0.1);
            border-radius: 3px;
        }

        .clip-item {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border-color);
            border-radius: 14px;
            padding: 10px 14px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 12px;
            transition: all 0.25s ease;
        }

        .clip-item.active-a {
            border-color: var(--deck-a-color);
            background: rgba(255, 59, 48, 0.06);
            box-shadow: 0 0 10px rgba(255, 59, 48, 0.08);
        }

        .clip-item.active-b {
            border-color: var(--deck-b-color);
            background: rgba(52, 199, 89, 0.06);
            box-shadow: 0 0 10px rgba(52, 199, 89, 0.08);
        }

        .clip-title {
            font-size: 13px;
            font-weight: 500;
            color: var(--text-color);
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
            flex-grow: 1;
        }

        .clip-item.active-a .clip-title {
            color: #ff453a;
            font-weight: 600;
        }

        .clip-item.active-b .clip-title {
            color: #30d158;
            font-weight: 600;
        }

        .clip-btn-group {
            display: flex;
            gap: 6px;
        }

        .clip-select-btn {
            border: 1px solid var(--border-color);
            border-radius: 8px;
            width: 32px;
            height: 32px;
            font-size: 12px;
            font-weight: 700;
            cursor: pointer;
            outline: none;
            background: rgba(255, 255, 255, 0.05);
            color: var(--text-muted);
            transition: all 0.2s ease;
        }

        .clip-select-btn:active {
            transform: scale(0.9);
        }

        .clip-select-btn.btn-to-a {
            border-color: rgba(255, 59, 48, 0.2);
        }

        .clip-select-btn.btn-to-a.active {
            background: var(--deck-a-color);
            color: white;
            border-color: var(--deck-a-color);
            box-shadow: 0 2px 8px rgba(255, 59, 48, 0.4);
        }

        .clip-select-btn.btn-to-b {
            border-color: rgba(52, 199, 89, 0.2);
        }

        .clip-select-btn.btn-to-b.active {
            background: var(--deck-b-color);
            color: white;
            border-color: var(--deck-b-color);
            box-shadow: 0 2px 8px rgba(52, 199, 89, 0.4);
        }

        .no-clips {
            text-align: center;
            font-size: 13px;
            color: var(--text-muted);
            padding: 20px;
            font-style: italic;
        }
    </style>
)html"
    R"html(
</head>
<body>
    <div class="wrapper">
        <header class="header">
            <div class="brand">
                <h1>CutWire Prism</h1>
                <div class="subtitle">Remote Console</div>
            </div>
            <div class="status-badge">
                <div id="statusDot" class="status-dot"></div>
                <span id="statusText">Connecting</span>
            </div>
        </header>

        <section class="deck-monitors">
            <div class="deck-card deck-a">
                <div class="deck-label">Deck A (PGM)</div>
                <div id="deckAClip" class="deck-clip-name">No Clip Loaded</div>
                <div class="deck-controls">
                    <button id="playBtnA" class="play-toggle" onclick="togglePlay('A')">
                        <svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>
                    </button>
                </div>
            </div>
            <div class="deck-card deck-b">
                <div class="deck-label">Deck B (PVW)</div>
                <div id="deckBClip" class="deck-clip-name">No Clip Loaded</div>
                <div class="deck-controls">
                    <button id="playBtnB" class="play-toggle" onclick="togglePlay('B')">
                        <svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>
                    </button>
                </div>
            </div>
        </section>

        <main class="main-panel">
            <div class="fader-container">
                <div class="fader-labels">
                    <span>DECK A</span>
                    <span id="mixPct">A: 100%</span>
                    <span>DECK B</span>
                </div>
                <div class="fader-slider-wrap">
                    <input type="range" id="faderSlider" class="fader-slider" min="0" max="100" value="0">
                </div>
            </div>
            
            <div class="action-buttons">
                <button id="cutBtn" class="btn btn-cut">CUT</button>
                <button id="autoBtn" class="btn btn-auto">AUTO</button>
            </div>

            <div>
                <div class="section-title">Transition settings</div>
                <div class="transition-config">
                    <div>
                        <select id="transitionCombo" class="select-style" onchange="setTransitionMode(this.value)">
                        </select>
                    </div>
                    <div>
                        <input type="number" id="durationSpin" class="input-style" min="0.1" max="10" step="0.1" value="1.0" onchange="setDuration(this.value)">
                    </div>
                </div>
            </div>
        </main>

        <section class="clips-panel">
            <div class="section-title">Project Clips</div>
            <div id="clipsList" class="clips-list">
                <div class="no-clips">No clips loaded in project.</div>
            </div>
        </section>
    </div>
)html"
    R"html(
    <script>
        const slider = document.getElementById('faderSlider');
        const mixPct = document.getElementById('mixPct');
        const cutBtn = document.getElementById('cutBtn');
        const autoBtn = document.getElementById('autoBtn');
        const transitionCombo = document.getElementById('transitionCombo');
        const durationSpin = document.getElementById('durationSpin');
        
        const statusDot = document.getElementById('statusDot');
        const statusText = document.getElementById('statusText');
        const deckAClip = document.getElementById('deckAClip');
        const deckBClip = document.getElementById('deckBClip');
        const playBtnA = document.getElementById('playBtnA');
        const playBtnB = document.getElementById('playBtnB');
        const clipsList = document.getElementById('clipsList');
        
        let isDragging = false;
        let isConnected = false;
        let lastClipsJson = "";
        let lastModesJson = "";

        const playIcon = '<svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>';
        const pauseIcon = '<svg viewBox="0 0 24 24"><path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z"/></svg>';

        function updateFaderUI(val) {
            if (val === 0) {
                mixPct.textContent = 'Deck A (100%)';
            } else if (val === 100) {
                mixPct.textContent = 'Deck B (100%)';
            } else {
                mixPct.textContent = `A: ${100 - val}% | B: ${val}%`;
            }
        }

        async function sendCommand(url) {
            try {
                await fetch(url);
            } catch (err) {
                console.error("API error:", err);
            }
        }

        slider.addEventListener('input', (e) => {
            const val = parseInt(e.target.value);
            updateFaderUI(val);
            sendCommand(`/api/fader?val=${val}`);
        });

        slider.addEventListener('mousedown', () => { isDragging = true; });
        slider.addEventListener('mouseup', () => { isDragging = false; });
        slider.addEventListener('touchstart', () => { isDragging = true; });
        slider.addEventListener('touchend', () => { isDragging = false; });

        cutBtn.addEventListener('click', () => sendCommand('/api/cut'));
        autoBtn.addEventListener('click', () => sendCommand('/api/auto'));

        function togglePlay(deck) {
            sendCommand(`/api/togglePlay${deck}`);
        }

        function setTransitionMode(index) {
            sendCommand(`/api/setTransitionMode?index=${index}`);
        }

        function setDuration(val) {
            sendCommand(`/api/setDuration?val=${val}`);
        }

        function selectClip(id, deck) {
            sendCommand(`/api/select${deck}?id=${id}`);
        }

        async function pollStatus() {
            try {
                const res = await fetch('/api/status');
                if (!res.ok) throw new Error("HTTP status " + res.status);
                const data = await res.json();
                
                if (!isConnected) {
                    isConnected = true;
                    statusDot.className = "status-dot connected";
                    statusText.textContent = "Live";
                }

                if (!isDragging) {
                    slider.value = data.fader;
                    updateFaderUI(data.fader);
                }

                if (data.transitionDuration !== undefined && document.activeElement !== durationSpin) {
                    durationSpin.value = data.transitionDuration.toFixed(1);
                }

                if (data.transitionModes) {
                    const modesJson = JSON.stringify(data.transitionModes);
                    if (modesJson !== lastModesJson) {
                        lastModesJson = modesJson;
                        transitionCombo.innerHTML = "";
                        data.transitionModes.forEach((mode, index) => {
                            const opt = document.createElement('option');
                            opt.value = index;
                            opt.textContent = mode;
                            transitionCombo.appendChild(opt);
                        });
                    }
                    if (data.transitionMode !== undefined) {
                        transitionCombo.value = data.transitionMode;
                    }
                }

                let clipAName = "No Clip Loaded";
                let clipBName = "No Clip Loaded";
                
                if (data.clips) {
                    const activeAClip = data.clips.find(c => c.id === data.activeA);
                    if (activeAClip) clipAName = activeAClip.name;
                    
                    const activeBClip = data.clips.find(c => c.id === data.activeB);
                    if (activeBClip) clipBName = activeBClip.name;
                }
                
                deckAClip.textContent = clipAName;
                deckBClip.textContent = clipBName;

                if (data.isPlayingA) {
                    playBtnA.classList.add('playing');
                    playBtnA.innerHTML = pauseIcon;
                } else {
                    playBtnA.classList.remove('playing');
                    playBtnA.innerHTML = playIcon;
                }

                if (data.isPlayingB) {
                    playBtnB.classList.add('playing');
                    playBtnB.innerHTML = pauseIcon;
                } else {
                    playBtnB.classList.remove('playing');
                    playBtnB.innerHTML = playIcon;
                }

                if (data.clips) {
                    const clipsJson = JSON.stringify(data.clips);
                    if (clipsJson !== lastClipsJson) {
                        lastClipsJson = clipsJson;
                        if (data.clips.length === 0) {
                            clipsList.innerHTML = '<div class="no-clips">No clips loaded in project.</div>';
                        } else {
                            clipsList.innerHTML = "";
                            data.clips.forEach(clip => {
                                const div = document.createElement('div');
                                div.className = `clip-item ${clip.activeA ? 'active-a' : ''} ${clip.activeB ? 'active-b' : ''}`;
                                
                                const title = document.createElement('span');
                                title.className = 'clip-title';
                                title.textContent = clip.name;
                                div.appendChild(title);
                                
                                const btnGroup = document.createElement('div');
                                btnGroup.className = 'clip-btn-group';
                                
                                const btnA = document.createElement('button');
                                btnA.className = `clip-select-btn btn-to-a ${clip.activeA ? 'active' : ''}`;
                                btnA.textContent = "A";
                                btnA.onclick = () => selectClip(clip.id, 'A');
                                btnGroup.appendChild(btnA);

                                const btnB = document.createElement('button');
                                btnB.className = `clip-select-btn btn-to-b ${clip.activeB ? 'active' : ''}`;
                                btnB.textContent = "B";
                                btnB.onclick = () => selectClip(clip.id, 'B');
                                btnGroup.appendChild(btnB);

                                div.appendChild(btnGroup);
                                clipsList.appendChild(div);
                            });
                        }
                    }
                }

            } catch (err) {
                console.error("Poll error:", err);
                isConnected = false;
                statusDot.className = "status-dot";
                statusText.textContent = "Offline";
            }
        }

        setInterval(pollStatus, 150);
    </script>
</body>
</html>)html");
}
