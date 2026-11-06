#include "app/remote/OscServer.h"

#include "app/AppController.h"
#include "app/show/ShowManager.h"
#include "app/transitions/TransitionController.h"

#include <QHostInfo>
#include <QNetworkDatagram>
#include <QSettings>
#include <QTimer>
#include <QUdpSocket>

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

void OscServer::dispatch(const OscProtocol::OscMessage &msg)
{
    if (!m_mainWindow)
        return;

    const QStringList parts = msg.address.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2 || parts.at(0) != QLatin1String("prism"))
        return;

    const QString &cmd = parts.at(1);
    const QVariantList &args = msg.args;
    TransitionController *transition = m_mainWindow->transitionController();

    if (parts.size() == 3 && (cmd == QLatin1String("live") || cmd == QLatin1String("staging"))) {
        handleDeck(cmd == QLatin1String("live") ? Deck::Live : Deck::Staging, parts.at(2), args);
    } else if (parts.size() == 2 && cmd == QLatin1String("cut")) {
        m_mainWindow->cut();
    } else if (parts.size() == 3 && cmd == QLatin1String("cue") && parts.at(2) == QLatin1String("stage")) {
        handleStage(args);
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

void OscServer::handleStage(const QVariantList &args)
{
    if (args.isEmpty())
        return;
    const QVariant &arg = args.first();
    if (isNumber(arg)) {
        const int index = arg.toInt() - 1;
        const QList<Cue *> &cues = m_mainWindow->showManager()->cues();
        if (index >= 0 && index < cues.size())
            m_mainWindow->stageCue(cues.at(index)->id());
    } else {
        m_mainWindow->stageCue(arg.toString());
    }
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
    auto fill = [&](DeckState &deck, const Cue *cue) {
        deck.input = cue ? cue->id() : QString();
        deck.slot = cue ? show->indexOf(cue->id()) + 1 : 0;
        deck.name = cue ? cue->name() : QString();
    };
    const bool livePlaying = s.live.playing, stagingPlaying = s.staging.playing;
    fill(s.live, show->liveCue());
    fill(s.staging, show->stagingCue());
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
