#include "ui/obs/ObsWebSocketClient.h"

#ifdef SWITCHX_HAVE_OBS_WS
#include <QWebSocket>
#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QUuid>
#endif

ObsWebSocketClient::ObsWebSocketClient(QObject *parent)
    : QObject(parent)
#ifdef SWITCHX_HAVE_OBS_WS
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
{
    connect(m_socket, &QWebSocket::connected, this, &ObsWebSocketClient::onSocketConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &ObsWebSocketClient::onSocketDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived,
            this, &ObsWebSocketClient::onTextMessageReceived);
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        m_lastError = m_socket->errorString();
        emit errorOccurred(m_lastError);
    });
}
#else
{
}
#endif

ObsWebSocketClient::~ObsWebSocketClient() {
    disconnectFromObs();
}

bool ObsWebSocketClient::isAvailable() {
#ifdef SWITCHX_HAVE_OBS_WS
    return true;
#else
    return false;
#endif
}

#ifdef SWITCHX_HAVE_OBS_WS

QString ObsWebSocketClient::nextRequestId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ObsWebSocketClient::buildAuthString(const QString &password,
                                            const QString &salt,
                                            const QString &challenge)
{
    const QByteArray secretHash = QCryptographicHash::hash(
        (password + salt).toUtf8(), QCryptographicHash::Sha256);
    const QString secret = secretHash.toBase64();

    const QByteArray authHash = QCryptographicHash::hash(
        (secret + challenge).toUtf8(), QCryptographicHash::Sha256);
    return QString::fromUtf8(authHash.toBase64());
}

#endif

void ObsWebSocketClient::connectToObs(const QString &host, quint16 port,
                                      const QString &password)
{
#ifndef SWITCHX_HAVE_OBS_WS
    Q_UNUSED(host);
    Q_UNUSED(port);
    m_lastError = QStringLiteral("Qt WebSockets not available at build time");
    emit errorOccurred(m_lastError);
    return;
#else
    disconnectFromObs();
    m_password = password;
    m_lastError.clear();

    const QUrl url(QStringLiteral("ws://%1:%2").arg(host).arg(port));
    m_socket->open(url);
#endif
}

void ObsWebSocketClient::disconnectFromObs() {
#ifdef SWITCHX_HAVE_OBS_WS
    m_pending.clear();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();
#endif
    m_identified = false;
    m_sceneNames.clear();
    m_currentProgramScene.clear();
    emit connectedChanged(false);
}

void ObsWebSocketClient::refreshSceneList() {
#ifdef SWITCHX_HAVE_OBS_WS
    sendRequest(QStringLiteral("GetSceneList"), {}, [this](const QJsonObject &responseData) {
        QStringList names;
        const QJsonArray scenes = responseData.value(QStringLiteral("scenes")).toArray();
        for (const QJsonValue &v : scenes) {
            const QString name = v.toObject().value(QStringLiteral("sceneName")).toString();
            if (!name.isEmpty()) names << name;
        }
        m_sceneNames = names;
        emit sceneListUpdated(m_sceneNames);
    });
#else
    Q_UNUSED(this);
#endif
}

void ObsWebSocketClient::setProgramScene(const QString &sceneName) {
#ifdef SWITCHX_HAVE_OBS_WS
    if (sceneName.isEmpty()) return;
    sendRequest(QStringLiteral("SetCurrentProgramScene"),
                {{QStringLiteral("sceneName"), sceneName}});
#else
    Q_UNUSED(sceneName);
#endif
}

#ifdef SWITCHX_HAVE_OBS_WS

void ObsWebSocketClient::onSocketConnected() {
}

void ObsWebSocketClient::onSocketDisconnected() {
    const bool wasConnected = m_identified;
    m_identified = false;
    m_pending.clear();
    if (wasConnected)
        emit connectedChanged(false);
}

void ObsWebSocketClient::sendIdentify(const QJsonObject &helloData, const QString &password)
{
    QJsonObject identify;
    identify.insert(QStringLiteral("rpcVersion"), helloData.value(QStringLiteral("rpcVersion")).toInt(1));
    identify.insert(QStringLiteral("eventSubscriptions"), 1 << 2); // EventSubscription::Scenes

    if (helloData.contains(QStringLiteral("authentication"))) {
        const QJsonObject auth = helloData.value(QStringLiteral("authentication")).toObject();
        identify.insert(QStringLiteral("authentication"),
                        buildAuthString(password,
                                        auth.value(QStringLiteral("salt")).toString(),
                                        auth.value(QStringLiteral("challenge")).toString()));
    }

    const QJsonObject msg{
        {QStringLiteral("op"), 1},
        {QStringLiteral("d"), identify}
    };
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void ObsWebSocketClient::sendRequest(const QString &requestType,
                                     const QJsonObject &requestData,
                                     std::function<void(const QJsonObject &)> handler)
{
    if (!m_identified) return;

    const QString requestId = nextRequestId();
    if (handler)
        m_pending.insert(requestId, PendingRequest{requestType, std::move(handler)});

    const QJsonObject msg{
        {QStringLiteral("op"), 6},
        {QStringLiteral("d"), QJsonObject{
            {QStringLiteral("requestType"), requestType},
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("requestData"), requestData}
        }}
    };
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void ObsWebSocketClient::onTextMessageReceived(const QString &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    const QJsonObject root = doc.object();
    const int op = root.value(QStringLiteral("op")).toInt(-1);
    const QJsonObject d = root.value(QStringLiteral("d")).toObject();

    switch (op) {
    case 0:
        sendIdentify(d, m_password);
        break;

    case 2:
        m_identified = true;
        emit connectedChanged(true);
        refreshSceneList();
        sendRequest(QStringLiteral("GetCurrentProgramScene"), {}, [this](const QJsonObject &data) {
            m_currentProgramScene = data.value(QStringLiteral("currentProgramSceneName")).toString();
            if (!m_currentProgramScene.isEmpty())
                emit programSceneChanged(m_currentProgramScene);
        });
        break;

    case 5: {
        const QString eventType = d.value(QStringLiteral("eventType")).toString();
        if (eventType == QStringLiteral("CurrentProgramSceneChanged")) {
            m_currentProgramScene =
                d.value(QStringLiteral("eventData")).toObject()
                    .value(QStringLiteral("sceneName")).toString();
            emit programSceneChanged(m_currentProgramScene);
        }
        break;
    }

    case 7: {
        const QString requestId = d.value(QStringLiteral("requestId")).toString();
        const QJsonObject status = d.value(QStringLiteral("requestStatus")).toObject();
        const bool ok = status.value(QStringLiteral("result")).toBool(false);

        if (!ok) {
            m_lastError = status.value(QStringLiteral("comment")).toString();
            if (m_lastError.isEmpty())
                m_lastError = QStringLiteral("OBS request failed");
            emit errorOccurred(m_lastError);
        }

        const auto it = m_pending.find(requestId);
        if (it != m_pending.end()) {
            if (ok && it->handler)
                it->handler(d.value(QStringLiteral("responseData")).toObject());
            m_pending.erase(it);
        }
        break;
    }

    default:
        break;
    }
}

#endif
