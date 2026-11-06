#include "app/dialogs/SourcePrompts.h"

#include "app/AppController.h"
#include "app/common/ThumbHelper.h"
#include "core/media/ThumbnailExtractor.h"
#include "core/sources/NdiSource.h"
#include "core/sources/WindowCaptureSource.h"
#include "ui/common/CameraEnumerator.h"
#include "ui/common/Icons.h"
#include "ui/common/QrCodeHelper.h"
#ifdef PRISM_HAVE_WEBRTC
#include "core/sources/WebRtcSource.h"
#include "core/webrtc/NetworkUtils.h"
#include "core/webrtc/WebRtcManager.h"
#include "core/webrtc/WebRtcPairing.h"
#endif

#include <QCameraDevice>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaDevices>
#include <QScreen>
#include <QSettings>
#include <QUrl>
#include <QUuid>

#include <numeric>

SourcePrompts::SourcePrompts(QObject *parent)
    : QObject(parent)
{
}

bool SourcePrompts::ndiAvailable() const { return NdiSource::isAvailable(); }

bool SourcePrompts::webrtcAvailable() const
{
#ifdef PRISM_HAVE_WEBRTC
    return WebRtcSource::isAvailable();
#else
    return false;
#endif
}

bool SourcePrompts::capturePickerNeeded() const
{
#ifdef Q_OS_LINUX
    return false;
#else
    return true;
#endif
}

bool SourcePrompts::webrtcPeerConnected() const
{
#ifdef PRISM_HAVE_WEBRTC
    return !m_webrtcToken.isEmpty() && WebRtcManager::instance().isPeerConnected(m_webrtcToken);
#else
    return false;
#endif
}

QString SourcePrompts::webrtcDefaultRelayUrl() const
{
#ifdef PRISM_HAVE_WEBRTC
    return QSettings(QStringLiteral("Prism"), QStringLiteral("WebRTC"))
        .value(QStringLiteral("relayUrl"), QString::fromLatin1(WebRtcPairing::kDefaultRelayUrl))
        .toString();
#else
    return {};
#endif
}

quint64 SourcePrompts::add(SourceDescriptor desc, const QImage &thumb)
{
    auto *app = AppController::instance();
    return app ? app->addSourceFromDescriptor(desc, thumb) : 0;
}

quint64 SourcePrompts::addSlideshow(const QString &folder, int intervalSeconds)
{
    if (folder.isEmpty())
        return 0;
    const QDir dir(folder);
    const QStringList imgs = dir.entryList({"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.webp"}, QDir::Files, QDir::Name);
    QImage thumb;
    if (!imgs.isEmpty())
        thumb = ThumbnailExtractor::extract(dir.absoluteFilePath(imgs.first()), 110, 65).toImage();
    if (thumb.isNull())
        thumb = ThumbHelper::makeIconThumb(Icons::Names::Folder);

    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Slideshow;
    desc.path = folder;
    desc.displayName = QFileInfo(folder).fileName();
    desc.slideshowIntervalMs = qBound(1, intervalSeconds, 60) * 1000;
    desc.slideshowEffect = 0;
    desc.slideshowTransitionMs = 800;
    return add(desc, thumb);
}

QStringList SourcePrompts::cameraLabels()
{
    QStringList labels;
    for (const CameraDeviceInfo &e : CameraEnumerator::listDevices())
        labels << e.label;
    return labels;
}

quint64 SourcePrompts::addCamera(int index)
{
    const QList<CameraDeviceInfo> devices = CameraEnumerator::listDevices();
    if (index < 0 || index >= devices.size())
        return 0;
    const CameraDeviceInfo &entry = devices.at(index);

    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Camera;
    desc.path = entry.id;
    desc.displayName = entry.isDefault ? tr("Default Camera") : entry.label.section("  [", 0, 0).trimmed();
    if (desc.displayName.isEmpty())
        desc.displayName = entry.id.isEmpty() ? tr("Default Camera") : entry.id;
    desc.cameraIndex = 0;
    if (!entry.isDefault) {
        const auto qtDevices = QMediaDevices::videoInputs();
        for (int i = 0; i < qtDevices.size(); ++i) {
            if (QString::fromUtf8(qtDevices[i].id()) == entry.id) {
                desc.cameraIndex = i;
                break;
            }
        }
    }
    return add(desc, ThumbHelper::makeIconThumb(Icons::Names::PhotoCamera));
}

quint64 SourcePrompts::addPortalCapture(const QString &kind)
{
    SourceDescriptor desc;
    const bool window = kind == QLatin1String("window");
    desc.kind = window ? SourceDescriptor::Kind::Window : SourceDescriptor::Kind::Screen;
    desc.displayName = window ? tr("Window / Tab") : tr("Screen Capture");
    desc.screenIndex = 0;
    desc.windowIndex = 0;
    desc.captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return add(desc, ThumbHelper::makeIconThumb(window ? Icons::Names::SelectWindow
                                                       : Icons::Names::DesktopWindows));
}

QStringList SourcePrompts::screenLabels() const
{
    QStringList labels;
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i)
        labels << tr("Screen %1 — %2").arg(i + 1).arg(screens[i]->name());
    return labels;
}

QStringList SourcePrompts::windowLabels()
{
    QStringList labels;
    for (const auto &w : WindowCaptureSource::capturableWindows())
        labels << (w.description().isEmpty() ? tr("(unnamed window)") : w.description());
    return labels;
}

quint64 SourcePrompts::addScreen(int index)
{
    if (index < 0 || index >= QGuiApplication::screens().size())
        return 0;
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Screen;
    desc.screenIndex = index;
    desc.displayName = tr("Screen %1").arg(index + 1);
    return add(desc, ThumbHelper::makeIconThumb(Icons::Names::DesktopWindows));
}

quint64 SourcePrompts::addWindow(int index)
{
    const auto windows = WindowCaptureSource::capturableWindows();
    if (index < 0 || index >= windows.size())
        return 0;
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Window;
    desc.windowIndex = index;
    desc.displayName = windows.at(index).description();
    return add(desc, ThumbHelper::makeIconThumb(Icons::Names::SelectWindow));
}

quint64 SourcePrompts::addShader(const QString &code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty())
        return 0;
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Shader;
    desc.shaderCode = trimmed;
    desc.displayName = tr("Shader");
    return add(desc, ThumbHelper::makeShaderThumb(trimmed));
}

QVariantList SourcePrompts::canvasPresets() const
{
    return {
        QVariantMap{{"label", QStringLiteral("16:9  (1280x720)")}, {"width", 1280}, {"height", 720}},
        QVariantMap{{"label", QStringLiteral("4:3  (1024x768)")}, {"width", 1024}, {"height", 768}},
        QVariantMap{{"label", QStringLiteral("1:1  (1080x1080)")}, {"width", 1080}, {"height", 1080}},
        QVariantMap{{"label", QStringLiteral("9:16  (1080x1920)")}, {"width", 1080}, {"height", 1920}},
    };
}

quint64 SourcePrompts::addCanvas(int width, int height, const QString &fill, const QColor &color)
{
    width = qBound(16, width, 16384);
    height = qBound(16, height, 16384);
    const int g = std::gcd(width, height);
    const QString ratioText = QStringLiteral("%1:%2").arg(width / g).arg(height / g);

    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Canvas;
    desc.canvasWidth = width;
    desc.canvasHeight = height;
    desc.canvasFill = SourceDescriptor::CanvasFill::Checkered;
    desc.color = Qt::white;
    QString fillLabel = tr("Checkered");
    if (fill == QLatin1String("transparent")) {
        desc.canvasFill = SourceDescriptor::CanvasFill::Transparent;
        fillLabel = tr("Transparent");
    } else if (fill == QLatin1String("color") && color.isValid()) {
        desc.canvasFill = SourceDescriptor::CanvasFill::Color;
        desc.color = color;
        fillLabel = color.name().toUpper();
    }
    desc.displayName = tr("Canvas %1 (%2)").arg(ratioText, fillLabel);
    return add(desc, ThumbHelper::makeCanvasThumb(ratioText, desc.canvasFill, desc.color));
}

QStringList SourcePrompts::discoverNdi()
{
    return NdiSource::isAvailable() ? NdiSource::discoverSources(2000) : QStringList{};
}

quint64 SourcePrompts::addNdi(const QString &name)
{
    if (name.isEmpty())
        return 0;
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Ndi;
    desc.path = name;
    desc.displayName = name;
    return add(desc, ThumbHelper::makeIconThumb(Icons::Names::Sensors));
}

QStringList SourcePrompts::webrtcInterfaces() const
{
    QStringList labels;
#ifdef PRISM_HAVE_WEBRTC
    for (const NetworkUtils::Ipv4Interface &iface : NetworkUtils::listIpv4Interfaces())
        labels << iface.label;
#endif
    return labels;
}

int SourcePrompts::webrtcDefaultInterface() const
{
#ifdef PRISM_HAVE_WEBRTC
    return NetworkUtils::defaultInterfaceIndex(NetworkUtils::listIpv4Interfaces());
#else
    return 0;
#endif
}

QVariantMap SourcePrompts::webrtcStart(const QString &relayUrl, int interfaceIndex)
{
#ifdef PRISM_HAVE_WEBRTC
    if (!WebRtcSource::isAvailable())
        return {{"ok", false}, {"error", tr("WebRTC is not available in this build.")}};

    QString relay;
    QString bind;
    if (!relayUrl.trimmed().isEmpty()) {
        const QUrl url(relayUrl.trimmed());
        if (!url.isValid() || url.host().isEmpty()
            || (url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss")))
            return {{"ok", false}, {"error", tr("Enter a valid relay WebSocket URL (ws:// or wss://).")}};
        relay = url.toString(QUrl::RemoveFragment);
        QSettings(QStringLiteral("Prism"), QStringLiteral("WebRTC")).setValue(QStringLiteral("relayUrl"), relay);
    } else {
        const auto ifaces = NetworkUtils::listIpv4Interfaces();
        if (interfaceIndex < 0 || interfaceIndex >= ifaces.size())
            return {{"ok", false}, {"error", tr("No active network interface with an IPv4 address was found.")}};
        bind = ifaces.at(interfaceIndex).address;
    }

    webrtcCancel();
    const WebRtcPairingInfo info = relay.isEmpty()
        ? WebRtcManager::instance().createSession(bind)
        : WebRtcManager::instance().createSession({}, relay);
    if (info.token.isEmpty()) {
        return {{"ok", false}, {"error", relay.isEmpty()
            ? tr("Could not start the WebRTC servers on %1. Check that ports %2 (signaling) and %3 (browser) are free.")
                  .arg(bind).arg(WebRtcPairing::kDefaultSigPort).arg(WebRtcPairing::kDefaultHttpPort)
            : tr("Could not connect to the relay server at %1. Check the URL and that the signaling server is running.")
                  .arg(relay)}};
    }

    if (!m_peerHooked) {
        m_peerHooked = true;
        connect(&WebRtcManager::instance(), &WebRtcManager::peerConnected, this, [this](const QString &token) {
            if (token == m_webrtcToken) emit webrtcPeerChanged();
        });
        connect(&WebRtcManager::instance(), &WebRtcManager::peerDisconnected, this, [this](const QString &token) {
            if (token == m_webrtcToken) emit webrtcPeerChanged();
        });
    }

    m_webrtcToken = info.token;
    m_webrtcRelayUrl = info.relayUrl;
    m_webrtcRelay = info.usesRelay();
    const QString qrPayload = WebRtcPairing::toQrUrl(WebRtcPairing::makePayload(
        info.host, info.sigPort, info.token, info.httpPort, info.relayUrl));
    m_webrtcQr = QrCodeHelper::toDataUrl(qrPayload, 5);
    m_webrtcThumb = QrCodeHelper::toImage(qrPayload, 4);
    emit webrtcPeerChanged();
    return {{"ok", true}, {"error", QString()}};
#else
    Q_UNUSED(relayUrl);
    Q_UNUSED(interfaceIndex);
    return {{"ok", false}, {"error", tr("WebRTC is not available in this build.")}};
#endif
}

quint64 SourcePrompts::webrtcFinish()
{
#ifdef PRISM_HAVE_WEBRTC
    if (m_webrtcToken.isEmpty())
        return 0;
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::WebRtc;
    desc.path = m_webrtcToken;
    desc.webrtcRelayUrl = m_webrtcRelayUrl;
    desc.displayName = tr("Phone Camera");
    const quint64 id = add(desc, m_webrtcThumb);
    m_webrtcToken.clear();
    m_webrtcQr.clear();
    emit webrtcPeerChanged();
    return id;
#else
    return 0;
#endif
}

void SourcePrompts::webrtcCancel()
{
#ifdef PRISM_HAVE_WEBRTC
    if (!m_webrtcToken.isEmpty())
        WebRtcManager::instance().destroySession(m_webrtcToken);
#endif
    m_webrtcToken.clear();
    m_webrtcQr.clear();
    emit webrtcPeerChanged();
}
