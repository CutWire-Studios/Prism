#include "app/remote/OscServer.h"

#include "app/AppController.h"
#include "app/show/ShowManager.h"
#include "app/transitions/TransitionController.h"

#include <QHostInfo>
#include <QMutexLocker>
#include <QNetworkDatagram>
#include <QSettings>
#include <QTimer>
#include <QUdpSocket>
#include <algorithm>

namespace {

constexpr int kPollIntervalMs = 100;

bool argToBool(const QVariant &v)
{
    return v.typeId() == QMetaType::QString ? v.toString() != QLatin1String("0") && !v.toString().isEmpty()
                                            : v.toDouble() != 0.0;
}

bool isNumber(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Float:
    case QMetaType::Double:
        return true;
    default:
        return false;
    }
}

constexpr int kMaxControls = 512;

bool isFloat(const QVariant &v)
{
    return v.typeId() == QMetaType::Float || v.typeId() == QMetaType::Double;
}

} // namespace

OscServer::OscServer(AppController *mainWindow, QObject *parent)
    : QObject(parent), m_mainWindow(mainWindow)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &OscServer::pollState);

    m_learnTimer = new QTimer(this);
    m_learnTimer->setSingleShot(true);
    connect(m_learnTimer, &QTimer::timeout, this, [this] {
        m_learning = false;
        emit learnTimedOut();
    });
}

OscServer::~OscServer()
{
    stop();
}

bool OscServer::applySettings()
{
    stop();

    QSettings settings;
    if (!settings.value(QStringLiteral("osc/enabled"), false).toBool())
        return true;

    m_port = quint16(settings.value(QStringLiteral("osc/port"), kDefaultPort).toUInt());
    m_feedbackPort = quint16(settings.value(QStringLiteral("osc/feedbackPort"), kDefaultFeedbackPort).toUInt());

    m_feedbackHost.clear();
    const QString host = settings.value(QStringLiteral("osc/feedbackHost")).toString().trimmed();
    if (!host.isEmpty() && !m_feedbackHost.setAddress(host)) {
        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress &addr : info.addresses()) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                m_feedbackHost = addr;
                break;
            }
        }
        if (m_feedbackHost.isNull())
            qWarning("OSC: could not resolve feedback host %s", qPrintable(host));
    }

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::AnyIPv4, m_port)) {
        qWarning("OSC: could not bind UDP port %u: %s", m_port, qPrintable(m_socket->errorString()));
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &OscServer::onReadyRead);

    if (!m_feedbackHost.isNull()) {
        m_state = State{};
        pollState();
        m_pollTimer->start();
    }
    return true;
}

void OscServer::stop()
{
    m_pollTimer->stop();
    delete m_socket;
    m_socket = nullptr;
}

void OscServer::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        for (const OscProtocol::OscMessage &msg : OscProtocol::decode(datagram.data()))
            dispatch(msg);
    }
}

double OscServer::controlValue(const QString &address) const
{
    QMutexLocker lock(&m_controlMutex);
    return m_controls.value(address).value;
}

QVariantList OscServer::recentControls() const
{
    QList<QPair<quint64, QVariantMap>> items;
    {
        QMutexLocker lock(&m_controlMutex);
        for (auto it = m_controls.cbegin(); it != m_controls.cend(); ++it)
            items.append({it->seq, QVariantMap{{QStringLiteral("address"), it.key()},
                                               {QStringLiteral("value"), it->value}}});
    }
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QVariantList out;
    for (const auto &item : items)
        out.append(item.second);
    return out;
}

void OscServer::startLearn(int timeoutMs)
{
    m_learning = true;
    m_learnTimer->start(timeoutMs);
}

void OscServer::cancelLearn()
{
    m_learning = false;
    m_learnTimer->stop();
}

void OscServer::recordControl(const OscProtocol::OscMessage &msg)
{
    bool found = false;
    double value = 0.0;
    for (const QVariant &arg : msg.args) {
        if (arg.typeId() == QMetaType::Bool || isNumber(arg)) {
            value = arg.toDouble();
            found = true;
            break;
        }
    }
    if (!found)
        return;

    {
        QMutexLocker lock(&m_controlMutex);
        if (!m_controls.contains(msg.address) && m_controls.size() >= kMaxControls)
            return;
        m_controls.insert(msg.address, {value, ++m_controlSeq});
    }

    if (m_learning) {
        cancelLearn();
        emit learned(msg.address);
    }
}

void OscServer::dispatch(const OscProtocol::OscMessage &msg)
{
    const QStringList parts = msg.address.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return;
    if (parts.at(0) != QLatin1String("prism")) {
        recordControl(msg);
        return;
    }
    if (!m_mainWindow || parts.size() < 2)
        return;

    const QString &cmd = parts.at(1);
    const QVariantList &args = msg.args;
    TransitionController *transition = m_mainWindow->transitionController();

    if (parts.size() == 3 && (cmd == QLatin1String("live") || cmd == QLatin1String("staging"))) {
        handleDeck(cmd == QLatin1String("live") ? Deck::Live : Deck::Staging, parts.at(2), args);
    } else if (parts.size() == 2 && cmd == QLatin1String("cut")) {
        m_mainWindow->cut();
    } else if (parts.size() == 3 && cmd == QLatin1String("scene") && parts.at(2) == QLatin1String("stage")) {
        handleStage(args);
    } else if (parts.size() == 3 && cmd == QLatin1String("scene") && parts.at(2) == QLatin1String("take")) {
        if (args.isEmpty()) {
            m_mainWindow->take();
        } else {
            const QString id = sceneIdFromArg(args.first());
            if (!id.isEmpty())
                m_mainWindow->stageAndTake(id);
        }
    } else if (parts.size() == 3 && cmd == QLatin1String("scene") && parts.at(2) == QLatin1String("next")) {
        m_mainWindow->stageRelative(1);
    } else if (parts.size() == 3 && cmd == QLatin1String("scene") && parts.at(2) == QLatin1String("prev")) {
        m_mainWindow->stageRelative(-1);
    } else if (parts.size() == 2 && cmd == QLatin1String("take")) {
        m_mainWindow->take();
    } else if (parts.size() == 2 && cmd == QLatin1String("fader")) {
        if (!args.isEmpty() && isNumber(args.first())) {
            const double v = args.first().toDouble();
            m_mainWindow->setFaderValue(isFloat(args.first()) ? qRound(v * 100.0) : int(v));
        }
    } else if (parts.size() == 2 && cmd == QLatin1String("panic")) {
        if (!args.isEmpty())
            m_mainWindow->mcpSetPanic(args.first().toString());
    } else if (parts.size() == 2 && cmd == QLatin1String("status")) {
        sendFullState();
    } else if (parts.size() == 3 && cmd == QLatin1String("transition") && transition && !args.isEmpty()) {
        const QVariant &arg = args.first();
        if (parts.at(2) == QLatin1String("mode")) {
            if (arg.typeId() == QMetaType::QString) {
                const int index = transition->transitionModeNames().indexOf(arg.toString());
                if (index >= 0)
                    transition->setTransitionModeIndex(index);
            } else if (isNumber(arg)) {
                transition->setTransitionModeIndex(arg.toInt());
            }
        } else if (parts.at(2) == QLatin1String("duration") && isNumber(arg)) {
            transition->setTransitionDuration(arg.toDouble());
        }
    }
}

void OscServer::handleDeck(Deck deck, const QString &cmd, const QVariantList &args)
{
    const int slotIndex = static_cast<int>(deck);
    if (cmd == QLatin1String("play")) {
        m_mainWindow->playDeck(slotIndex, args.isEmpty() || argToBool(args.first()));
    } else if (cmd == QLatin1String("pause")) {
        m_mainWindow->playDeck(slotIndex, false);
    } else if (cmd == QLatin1String("toggle")) {
        m_mainWindow->togglePlay(slotIndex);
    } else if (cmd == QLatin1String("seek")) {
        if (!args.isEmpty() && isNumber(args.first()))
            m_mainWindow->seekDeck(slotIndex, args.first().toDouble());
    }
}

QString OscServer::sceneIdFromArg(const QVariant &arg) const
{
    if (!isNumber(arg))
        return arg.toString();
    const int index = arg.toInt() - 1;
    const QList<Scene *> &scenes = m_mainWindow->showManager()->scenes();
    return index >= 0 && index < scenes.size() ? scenes.at(index)->id() : QString();
}

void OscServer::handleStage(const QVariantList &args)
{
    if (args.isEmpty())
        return;
    const QString id = sceneIdFromArg(args.first());
    if (!id.isEmpty())
        m_mainWindow->stageScene(id);
}

OscServer::State OscServer::captureState() const
{
    State s;
    if (!m_mainWindow)
        return s;

    s.fader = m_mainWindow->faderValue();
    s.live.playing = m_mainWindow->isPlayingLive();
    s.staging.playing = m_mainWindow->isPlayingStaging();
    s.panic = m_mainWindow->mcpPanicMode();
    if (TransitionController *transition = m_mainWindow->transitionController()) {
        s.mode = transition->currentModeIndex();
        s.duration = transition->currentDurationSecs();
    }

    const ShowManager *show = m_mainWindow->showManager();
    auto fill = [&](DeckState &deck, const Scene *scene) {
        deck.input = scene ? scene->id() : QString();
        deck.slot = scene ? show->indexOf(scene->id()) + 1 : 0;
        deck.name = scene ? scene->name() : QString();
    };
    const bool livePlaying = s.live.playing, stagingPlaying = s.staging.playing;
    fill(s.live, show->liveScene());
    fill(s.staging, show->stagingScene());
    s.live.playing = livePlaying;
    s.staging.playing = stagingPlaying;
    return s;
}

void OscServer::pollState()
{
    const State now = captureState();
    const State &before = m_state;

    if (now.fader != before.fader) {
        send(QStringLiteral("/prism/fader"), {float(now.fader / 100.0)});
    }
    sendDeckDiff("live", now.live, before.live);
    if (now.live.slot != before.live.slot || now.live.name != before.live.name || now.live.input != before.live.input)
        send(QStringLiteral("/prism/scene/live"), {now.live.slot, now.live.name});
    if (now.staging.slot != before.staging.slot || now.staging.name != before.staging.name
        || now.staging.input != before.staging.input)
        send(QStringLiteral("/prism/scene/staging"), {now.staging.slot, now.staging.name});
    sendDeckDiff("staging", now.staging, before.staging);
    if (now.mode != before.mode)
        send(QStringLiteral("/prism/transition/mode"), {now.mode});
    if (now.duration != before.duration)
        send(QStringLiteral("/prism/transition/duration"), {float(now.duration)});
    if (now.panic != before.panic)
        send(QStringLiteral("/prism/panic"), {now.panic});

    m_state = now;
}

void OscServer::sendDeckDiff(const char *deck, const DeckState &now, const DeckState &before)
{
    const QString prefix = QStringLiteral("/prism/") + QLatin1String(deck);
    if (now.slot != before.slot || now.input != before.input)
        send(prefix + QStringLiteral("/slot"), {now.slot});
    if (now.name != before.name || now.input != before.input)
        send(prefix + QStringLiteral("/name"), {now.name});
    if (now.playing != before.playing || now.input != before.input)
        send(prefix + QStringLiteral("/playing"), {int(now.playing)});
}

void OscServer::sendFullState()
{
    if (m_feedbackHost.isNull())
        return;
    m_state = State{};
    pollState();
}

void OscServer::send(const QString &address, const QVariantList &args)
{
    if (!m_socket || m_feedbackHost.isNull())
        return;
    m_socket->writeDatagram(OscProtocol::encode({address, args}), m_feedbackHost, m_feedbackPort);
}
