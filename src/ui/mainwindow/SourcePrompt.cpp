#include "ui/mainwindow/SourcePrompt.h"
#include "ui/common/ThumbHelper.h"
#include "ui/common/MaterialSymbols.h"
#include "ui/common/CameraEnumerator.h"
#ifndef Q_OS_LINUX
#include "ui/common/CapturePicker.h"
#endif
#include "ui/editors/ShaderEditDialog.h"
#include "ui/editors/HtmlEditDialog.h"
#include "ui/editors/TextEditDialog.h"
#include "ui/common/ThumbHelper.h"
#include "core/media/ThumbnailExtractor.h"
#include "core/sources/NdiSource.h"
#ifdef PRISM_HAVE_WEBRTC
#include "core/webrtc/WebRtcManager.h"
#include "core/sources/WebRtcSource.h"
#include "core/webrtc/WebRtcPairing.h"
#include "core/webrtc/NetworkUtils.h"
#include "core/webrtc/FirewallUtils.h"
#include "ui/common/QrCodeHelper.h"
#endif

#include <QMenu>
#include <QAction>
#include <QWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSettings>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <numeric>
#include <optional>

namespace SourcePrompt {

namespace {

bool promptSlideshow(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    QString folder = QFileDialog::getExistingDirectory(parent, "Select Image Folder for Slideshow");
    if (folder.isEmpty()) return false;

    bool ok = false;
    int interval = QInputDialog::getInt(parent, "Slideshow Interval",
                                        "Seconds per slide:", 3, 1, 60, 1, &ok);
    if (!ok) return false;

    QDir dir(folder);
    QStringList imgs = dir.entryList({"*.png","*.jpg","*.jpeg","*.bmp","*.webp"},
                                     QDir::Files, QDir::Name);
    if (!imgs.isEmpty())
        thumb = ThumbnailExtractor::extract(dir.absoluteFilePath(imgs.first()), 110, 65);
    if (thumb.isNull()) thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::Folder);

    desc.kind                = SourceDescriptor::Kind::Slideshow;
    desc.path                = folder;
    desc.displayName         = QFileInfo(folder).fileName();
    desc.slideshowIntervalMs = interval * 1000;
    desc.slideshowEffect = 0;
    desc.slideshowTransitionMs = 800;
    return true;
}

bool promptCamera(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    const auto devices = CameraEnumerator::listDevices(parent);

    QStringList names;
    for (const auto &e : devices)
        names << e.label;

    bool ok = false;
    QString chosen = QInputDialog::getItem(parent, "Select Camera",
                                           "Camera device:", names, 0, false, &ok);
    if (!ok) return false;

    const int idx = names.indexOf(chosen);
    if (idx < 0 || idx >= devices.size())
        return false;
    const CameraDeviceInfo &entry = devices.at(idx);

    const auto qtDevices = QMediaDevices::videoInputs();
    desc.kind        = SourceDescriptor::Kind::Camera;
    desc.path        = entry.id;
    desc.displayName = entry.isDefault ? "Default Camera"
                     : entry.label.section("  [", 0, 0).trimmed();
    if (desc.displayName.isEmpty())
        desc.displayName = entry.id.isEmpty() ? "Default Camera" : entry.id;
    desc.cameraIndex = 0;
    if (!entry.isDefault) {
        for (int i = 0; i < qtDevices.size(); ++i) {
            if (QString::fromUtf8(qtDevices[i].id()) == entry.id) {
                desc.cameraIndex = i;
                break;
            }
        }
    }

    thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::PhotoCamera);
    return true;
}

bool promptScreen(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
#ifndef Q_OS_LINUX
    if (!CapturePicker::prompt(parent, desc, SourceDescriptor::Kind::Screen))
        return false;
    thumb = ThumbHelper::makeIconThumb(
        desc.kind == SourceDescriptor::Kind::Window
            ? MaterialSymbols::Names::SelectWindow
            : MaterialSymbols::Names::DesktopWindows);
    return true;
#else
    Q_UNUSED(parent);
    desc.kind        = SourceDescriptor::Kind::Screen;
    desc.displayName = "Screen Capture";
    desc.screenIndex = 0;
    // Stable id so the portal selection is remembered across recreations.
    desc.captureId   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::DesktopWindows);
    return true;
#endif
}

bool promptWindow(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
#ifndef Q_OS_LINUX
    if (!CapturePicker::prompt(parent, desc, SourceDescriptor::Kind::Window))
        return false;
    thumb = ThumbHelper::makeIconThumb(
        desc.kind == SourceDescriptor::Kind::Window
            ? MaterialSymbols::Names::SelectWindow
            : MaterialSymbols::Names::DesktopWindows);
    return true;
#else
    Q_UNUSED(parent);
    desc.kind        = SourceDescriptor::Kind::Window;
    desc.displayName = "Window / Tab";
    desc.windowIndex = 0;
    // Stable id so the portal selection is remembered across recreations.
    desc.captureId   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::SelectWindow);
    return true;
#endif
}

bool promptCanvas(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    struct CanvasPreset { const char *label; int width, height; };
    const CanvasPreset presets[] = {
        {"16:9  (1280x720)",  1280, 720},
        {"4:3  (1024x768)",   1024, 768},
        {"1:1  (1080x1080)", 1080, 1080},
        {"9:16  (1080x1920)",1080, 1920},
    };

    QStringList options;
    for (const auto &p : presets) options << QString::fromUtf8(p.label);
    options << "Custom…";

    bool ok = false;
    const QString choice = QInputDialog::getItem(parent, "Canvas",
                                                  "Aspect ratio:", options, 0, false, &ok);
    if (!ok || choice.isEmpty()) return false;

    int width = 1280, height = 720;
    if (choice == "Custom…") {
        width  = QInputDialog::getInt(parent, "Canvas Width",  "Width:",  1280, 16, 16384, 1, &ok); if (!ok) return false;
        height = QInputDialog::getInt(parent, "Canvas Height", "Height:", 720,  16, 16384, 1, &ok); if (!ok) return false;
    } else {
        for (const auto &p : presets)
            if (choice == QString::fromUtf8(p.label)) { width = p.width; height = p.height; break; }
    }

    const int g = std::gcd(width, height);
    const QString ratioText = QString("%1:%2").arg(width / g).arg(height / g);

    const QStringList fillOptions = {"Checkered", "Transparent", "Color"};
    const QString fillChoice = QInputDialog::getItem(parent, "Canvas Fill",
                                                      "Fill type:", fillOptions, 0, false, &ok);
    if (!ok || fillChoice.isEmpty()) return false;

    desc.kind = SourceDescriptor::Kind::Canvas;
    desc.canvasWidth = width; desc.canvasHeight = height;
    desc.canvasFill  = SourceDescriptor::CanvasFill::Checkered;
    desc.color       = Qt::white;
    QString fillLabel = "Checkered";

    if (fillChoice == "Transparent") {
        desc.canvasFill = SourceDescriptor::CanvasFill::Transparent;
        fillLabel = "Transparent";
    } else if (fillChoice == "Color") {
        QColor c = QColorDialog::getColor(Qt::white, parent, "Pick Canvas Color");
        if (!c.isValid()) return false;
        desc.canvasFill = SourceDescriptor::CanvasFill::Color;
        desc.color = c;
        fillLabel = c.name().toUpper();
    }

    desc.displayName = QString("Canvas %1 (%2)").arg(ratioText, fillLabel);
    thumb = ThumbHelper::makeCanvasThumb(ratioText, desc.canvasFill, desc.color);
    return true;
}

bool promptShader(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    ShaderEditDialog dlg(QString(), parent);
    if (dlg.exec() != QDialog::Accepted) return false;
    QString code = dlg.resultCode().trimmed();
    if (code.isEmpty()) return false;

    desc.kind        = SourceDescriptor::Kind::Shader;
    desc.shaderCode  = code;
    desc.displayName = "Shader";
    thumb = ThumbHelper::makeShaderThumb(code);
    return true;
}

bool promptHtml(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    HtmlEditDialog dlg(QString(), QString(), parent);
    if (dlg.exec() != QDialog::Accepted) return false;

    const QString workspace = dlg.resultWorkspaceJson();
    const QString bakedHtml = dlg.resultBakedHtml().trimmed();
    const QString filePath  = dlg.resultFilePath();
    if (workspace.isEmpty() && filePath.isEmpty() && bakedHtml.isEmpty())
        return false;

    desc.kind          = SourceDescriptor::Kind::Html;
    desc.htmlWorkspace = workspace;
    desc.htmlContent   = workspace.isEmpty() ? dlg.resultHtml().trimmed() : bakedHtml;
    desc.path          = workspace.isEmpty() ? filePath : QString();
    desc.displayName   = (!filePath.isEmpty() && workspace.isEmpty())
                             ? QFileInfo(filePath).fileName()
                             : QStringLiteral("HTML Overlay");
    thumb = ThumbHelper::makeHtmlThumb(desc.htmlContent, desc.path);
    return true;
}

bool promptText(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    TextEditDialog dlg({}, parent);
    if (dlg.exec() != QDialog::Accepted)
        return false;

    desc = dlg.resultDescriptor();
    if (desc.textTemplate.trimmed().isEmpty())
        return false;

    thumb = ThumbHelper::makeTextThumb(desc.textTemplate, desc.color);
    return true;
}

bool promptNdi(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    if (!NdiSource::isAvailable()) {
        QMessageBox::warning(parent, QObject::tr("NDI Input"),
            QObject::tr("NDI is not available. Install the NDI SDK and rebuild CutWire Prism with -DNDI_ROOT=…"));
        return false;
    }

    QStringList sources = NdiSource::discoverSources(2000);
    if (sources.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("NDI Input"),
            QObject::tr("No NDI sources found on the network.\n\n"
               "Make sure another app (CutWire Prism program output, OBS, phone NDI app) is sending NDI, "
               "then try again."));
        return false;
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(parent, QObject::tr("Select NDI Source"),
                                                 QObject::tr("NDI source:"), sources, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return false;

    desc.kind        = SourceDescriptor::Kind::Ndi;
    desc.path        = chosen;
    desc.displayName = chosen;
    thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::Sensors);
    return true;
}

#ifdef PRISM_HAVE_WEBRTC
static QJsonObject webRtcQrPayload(const WebRtcPairingInfo &info) {
    return WebRtcPairing::makePayload(
        info.host, info.sigPort, info.token, info.httpPort, info.relayUrl);
}

static QString normalizeRelayUrl(const QString &raw) {
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
        return {};
    QUrl url(trimmed);
    if (!url.isValid() || url.host().isEmpty())
        return {};
    if (url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss"))
        return {};
    return url.toString(QUrl::RemoveFragment);
}

bool showWebRtcConnectionDialog(QWidget *parent, const WebRtcPairingInfo &info, bool destroyOnCancel) {
    const QString qrPayload = WebRtcPairing::toQrUrl(webRtcQrPayload(info));

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Phone Camera (WebRTC)"));
    dlg.setMinimumWidth(360);

    auto *qrLabel = new QLabel(&dlg);
    qrLabel->setAlignment(Qt::AlignCenter);
    qrLabel->setPixmap(QrCodeHelper::toPixmap(qrPayload, 5));

    auto *statusLabel = new QLabel(QObject::tr("Waiting for phone…"), &dlg);
    statusLabel->setAlignment(Qt::AlignCenter);

    auto *hintLabel = new QLabel(
        info.usesRelay()
            ? QObject::tr("Open CutWire Prism on your PC first and keep the pairing dialog open, then tap Start here.\n"
                          "Scan with your phone camera for the browser streamer, or use the CutWire Prism app.\n"
                          "Internet pairing may require STUN/TURN for media on different networks.")
            : QObject::tr("Scan with your phone camera to open the browser streamer, or scan with the CutWire Prism app to pair natively.\n"
                          "Your phone may warn about the self-signed certificate — accept it to enable the camera."),
        &dlg);
    hintLabel->setWordWrap(true);
    hintLabel->setAlignment(Qt::AlignCenter);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);

    QObject::connect(&WebRtcManager::instance(), &WebRtcManager::peerConnected, &dlg,
        [info, statusLabel](const QString &token) {
            if (token != info.token) return;
            statusLabel->setText(QObject::tr("Connected"));
        });
    QObject::connect(&WebRtcManager::instance(), &WebRtcManager::peerDisconnected, &dlg,
        [info, statusLabel](const QString &token) {
            if (token != info.token) return;
            statusLabel->setText(QObject::tr("Waiting for phone…"));
        });

    if (WebRtcManager::instance().isPeerConnected(info.token))
        statusLabel->setText(QObject::tr("Connected"));

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(qrLabel);
    layout->addWidget(statusLabel);
    layout->addWidget(hintLabel);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) {
        if (destroyOnCancel)
            WebRtcManager::instance().destroySession(info.token);
        return false;
    }
    return true;
}

bool ensureWebRtcFirewall(QWidget *parent) {
    const QList<quint16> ports = {
        WebRtcPairing::kDefaultSigPort,
        WebRtcPairing::kDefaultHttpPort
    };
    const FirewallUtils::Status fw = FirewallUtils::detect();
    if (!fw.active)
        return true;

    QString fwErr;
    if (FirewallUtils::ensurePortsOpen(parent, ports, fwErr, QObject::tr("phone pairing")))
        return true;

    const auto cont = QMessageBox::warning(
        parent,
        QObject::tr("Firewall"),
        QObject::tr("Could not open firewall port(s).\n%1\n\n"
                      "Continue anyway? Your phone may not be able to connect.")
            .arg(fwErr.isEmpty() ? QObject::tr("Permission denied or cancelled.")
                                 : fwErr),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    return cont == QMessageBox::Yes;
}

QString promptWebRtcBindAddress(QWidget *parent) {
    const QList<NetworkUtils::Ipv4Interface> ifaces = NetworkUtils::listIpv4Interfaces();
    if (ifaces.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
            QObject::tr("No active network interface with an IPv4 address was found."));
        return {};
    }

    return NetworkUtils::promptInterface(
        parent, ifaces, NetworkUtils::defaultInterfaceIndex(ifaces));
}

struct WebRtcSetupChoice {
    QString relayUrl;
    QString bindAddress;
};

static std::optional<WebRtcSetupChoice> promptWebRtcSetup(QWidget *parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Phone Camera (WebRTC)"));
    dlg.setMinimumWidth(420);

    auto *localRadio = new QRadioButton(QObject::tr("Local network (LAN)"), &dlg);
    auto *relayRadio = new QRadioButton(QObject::tr("Public relay server"), &dlg);
    localRadio->setChecked(true);

    auto *modeGroup = new QButtonGroup(&dlg);
    modeGroup->addButton(localRadio, 0);
    modeGroup->addButton(relayRadio, 1);

    auto *relayEdit = new QLineEdit(&dlg);
    relayEdit->setPlaceholderText(QString::fromLatin1(WebRtcPairing::kDefaultRelayUrl));
    relayEdit->setText(QSettings(QStringLiteral("Prism"), QStringLiteral("WebRTC"))
                           .value(QStringLiteral("relayUrl"),
                                  QString::fromLatin1(WebRtcPairing::kDefaultRelayUrl))
                           .toString());
    relayEdit->setEnabled(false);

    QObject::connect(relayRadio, &QRadioButton::toggled, relayEdit, &QLineEdit::setEnabled);

    auto *relayHint = new QLabel(
        QObject::tr("Run scripts/webrtc_signaling_server.py on your VPS, then enter its WebSocket URL here."),
        &dlg);
    relayHint->setWordWrap(true);
    relayHint->setEnabled(false);
    QObject::connect(relayRadio, &QRadioButton::toggled, relayHint, &QLabel::setEnabled);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(localRadio);
    layout->addWidget(relayRadio);
    layout->addWidget(relayEdit);
    layout->addWidget(relayHint);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;

    WebRtcSetupChoice choice;
    if (relayRadio->isChecked()) {
        choice.relayUrl = normalizeRelayUrl(relayEdit->text());
        if (choice.relayUrl.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
                QObject::tr("Enter a valid relay WebSocket URL (ws:// or wss://)."));
            return std::nullopt;
        }
        QSettings(QStringLiteral("Prism"), QStringLiteral("WebRTC"))
            .setValue(QStringLiteral("relayUrl"), choice.relayUrl);
        return choice;
    }

    choice.bindAddress = promptWebRtcBindAddress(parent);
    if (choice.bindAddress.isEmpty())
        return std::nullopt;
    return choice;
}

bool promptWebRtc(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    if (!WebRtcSource::isAvailable()) {
        QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
            QObject::tr("WebRTC is not available in this build.\n"
                        "Rebuild with -DPRISM_WITH_WEBRTC=ON and install qt6-websockets."));
        return false;
    }

    const auto setup = promptWebRtcSetup(parent);
    if (!setup)
        return false;

    const bool useRelay = !setup->relayUrl.isEmpty();
    if (!useRelay && !ensureWebRtcFirewall(parent))
        return false;

    const WebRtcPairingInfo info = useRelay
        ? WebRtcManager::instance().createSession({}, setup->relayUrl)
        : WebRtcManager::instance().createSession(setup->bindAddress);
    if (info.token.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
            useRelay
                ? QObject::tr("Could not connect to the relay server at %1.\n"
                              "Check the URL and that the signaling server is running.")
                    .arg(setup->relayUrl)
                : QObject::tr("Could not start the WebRTC servers on %1.\n"
                              "Check that ports %2 (signaling) and %3 (browser) are free.")
                    .arg(setup->bindAddress)
                    .arg(WebRtcPairing::kDefaultSigPort)
                    .arg(WebRtcPairing::kDefaultHttpPort));
        return false;
    }

    const QString qrPayload = WebRtcPairing::toQrUrl(webRtcQrPayload(info));

    if (!showWebRtcConnectionDialog(parent, info, true))
        return false;

    desc.kind            = SourceDescriptor::Kind::WebRtc;
    desc.path            = info.token;
    desc.webrtcRelayUrl  = info.relayUrl;
    desc.displayName     = QObject::tr("Phone Camera");
    thumb = QrCodeHelper::toPixmap(qrPayload, 4);
    return true;
}

bool reconnectWebRtcDialog(QWidget *parent, const QString &sessionToken, const QString &relayUrl) {
    if (sessionToken.isEmpty())
        return false;

    if (!WebRtcSource::isAvailable()) {
        QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
            QObject::tr("WebRTC is not available in this build.\n"
                        "Rebuild with -DPRISM_WITH_WEBRTC=ON and install qt6-websockets."));
        return false;
    }

    const bool useRelay = !relayUrl.isEmpty();
    QString bindAddress;
    if (!useRelay) {
        bindAddress = WebRtcManager::instance().bindAddress();
        if (!WebRtcManager::instance().hasSession(sessionToken) || bindAddress.isEmpty()) {
            bindAddress = promptWebRtcBindAddress(parent);
            if (bindAddress.isEmpty())
                return false;
        }
        if (!ensureWebRtcFirewall(parent))
            return false;
    }

    const WebRtcPairingInfo info = useRelay
        ? WebRtcManager::instance().ensureSession({}, sessionToken, relayUrl)
        : WebRtcManager::instance().ensureSession(bindAddress, sessionToken);
    if (info.token.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
            useRelay
                ? QObject::tr("Could not connect to the relay server at %1.\n"
                              "Check the URL and that the signaling server is running.")
                    .arg(relayUrl)
                : QObject::tr("Could not start the WebRTC servers on %1.\n"
                              "Check that ports %2 (signaling) and %3 (browser) are free.")
                    .arg(bindAddress)
                    .arg(WebRtcPairing::kDefaultSigPort)
                    .arg(WebRtcPairing::kDefaultHttpPort));
        return false;
    }

    return showWebRtcConnectionDialog(parent, info, false);
}
#endif

} // namespace

#ifdef PRISM_HAVE_WEBRTC
bool reconnectWebRtc(QWidget *parent, const QString &sessionToken, const QString &relayUrl) {
    return reconnectWebRtcDialog(parent, sessionToken, relayUrl);
}
#endif

bool prompt(SourceDescriptor::Kind kind, QWidget *parent,
            SourceDescriptor &outDesc, QPixmap &outThumb)
{
    switch (kind) {
    case SourceDescriptor::Kind::Slideshow:
        return promptSlideshow(parent, outDesc, outThumb);
    case SourceDescriptor::Kind::Camera:
        return promptCamera(parent, outDesc, outThumb);
    case SourceDescriptor::Kind::Screen:
        return promptScreen(parent, outDesc, outThumb);
    case SourceDescriptor::Kind::Window:
        return promptWindow(parent, outDesc, outThumb);
    case SourceDescriptor::Kind::Canvas:
        return promptCanvas(parent, outDesc, outThumb);
    case SourceDescriptor::Kind::Shader:
        return promptShader(parent, outDesc, outThumb);
    case SourceDescriptor::Kind::Html:
        return promptHtml(parent, outDesc, outThumb);
    case SourceDescriptor::Kind::Text:
        return promptText(parent, outDesc, outThumb);
    case SourceDescriptor::Kind::Ndi:
        return promptNdi(parent, outDesc, outThumb);
    case SourceDescriptor::Kind::WebRtc:
#ifdef PRISM_HAVE_WEBRTC
        return promptWebRtc(parent, outDesc, outThumb);
#else
        Q_UNUSED(parent);
        Q_UNUSED(outDesc);
        Q_UNUSED(outThumb);
        return false;
#endif
    default:
        return false;
    }
}

void buildMenu(QMenu *menu,
               std::function<void()> onFile,
               std::function<void()> onUrl,
               std::function<void(SourceDescriptor::Kind)> onKind,
               bool ndiAvailable,
               bool webrtcAvailable)
{
    if (!menu) return;

    auto addIconAction = [&](const char *iconName, const QString &text, auto slot) {
        QAction *action = menu->addAction(text, std::move(slot));
        MaterialSymbols::setActionIcon(action, iconName);
        return action;
    };

    addIconAction(MaterialSymbols::Names::Movie, QObject::tr("Media File…"), std::move(onFile));
    addIconAction(MaterialSymbols::Names::Link, QObject::tr("Video URL…"), std::move(onUrl));
    addIconAction(MaterialSymbols::Names::Folder, QObject::tr("Slideshow…"),
                  [onKind]() { onKind(SourceDescriptor::Kind::Slideshow); });
    menu->addSeparator();
    addIconAction(MaterialSymbols::Names::PhotoCamera, QObject::tr("Camera…"),
                  [onKind]() { onKind(SourceDescriptor::Kind::Camera); });
    addIconAction(MaterialSymbols::Names::DesktopWindows, QObject::tr("Screen Capture…"),
                  [onKind]() { onKind(SourceDescriptor::Kind::Screen); });
    addIconAction(MaterialSymbols::Names::SelectWindow, QObject::tr("Window / Tab…"),
                  [onKind]() { onKind(SourceDescriptor::Kind::Window); });
    menu->addSeparator();
    addIconAction(MaterialSymbols::Names::CropSquare, QObject::tr("Canvas…"),
                  [onKind]() { onKind(SourceDescriptor::Kind::Canvas); });
    addIconAction(MaterialSymbols::Names::Grain, QObject::tr("Shader…"),
                  [onKind]() { onKind(SourceDescriptor::Kind::Shader); });
    addIconAction(MaterialSymbols::Names::Language, QObject::tr("HTML Overlay…"),
                  [onKind]() { onKind(SourceDescriptor::Kind::Html); });
    addIconAction(MaterialSymbols::Names::TextFields, QObject::tr("Text…"),
                  [onKind]() { onKind(SourceDescriptor::Kind::Text); });
    QAction *ndiAction = addIconAction(MaterialSymbols::Names::Sensors, QObject::tr("NDI Source…"),
                                       [onKind]() { onKind(SourceDescriptor::Kind::Ndi); });
    ndiAction->setEnabled(ndiAvailable);
    if (!ndiAvailable) {
        ndiAction->setToolTip(QObject::tr(
            "NDI SDK not found at build time. Install the NDI SDK and rebuild with -DNDI_ROOT=…"));
    }
    QAction *webrtcAction = addIconAction(MaterialSymbols::Names::Smartphone,
                                          QObject::tr("Phone Camera (WebRTC)…"),
                                          [onKind]() { onKind(SourceDescriptor::Kind::WebRtc); });
    webrtcAction->setEnabled(webrtcAvailable);
    if (!webrtcAvailable) {
        webrtcAction->setToolTip(QObject::tr(
            "WebRTC not enabled at build time. Rebuild with -DPRISM_WITH_WEBRTC=ON."));
    }
}

} // namespace SourcePrompt
