#include "ui/SourcePrompt.h"
#include "ui/ThumbHelper.h"
#include "ui/ShaderEditDialog.h"
#include "ui/HtmlEditDialog.h"
#include "ui/TextEditDialog.h"
#include "ui/ThumbHelper.h"
#include "core/ThumbnailExtractor.h"
#include "core/NdiSource.h"
#ifdef SWITCHX_HAVE_WEBRTC
#include "core/WebRtcManager.h"
#include "core/WebRtcSource.h"
#include "core/WebRtcPairing.h"
#include "core/NetworkUtils.h"
#include "core/FirewallUtils.h"
#include "ui/QrCodeHelper.h"
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
#include <QEventLoop>
#include <QTimer>
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

#include <glob.h>
#include <algorithm>
#include <numeric>

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
    if (thumb.isNull()) thumb = ThumbHelper::makeIconThumb("📁");

    desc.kind                = SourceDescriptor::Kind::Slideshow;
    desc.path                = folder;
    desc.displayName         = QFileInfo(folder).fileName();
    desc.slideshowIntervalMs = interval * 1000;
    desc.slideshowEffect = 0;
    desc.slideshowTransitionMs = 800;
    return true;
}

bool promptCamera(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    auto qtDevices = QMediaDevices::videoInputs();
    if (qtDevices.isEmpty()) {
        QEventLoop loop;
        QTimer::singleShot(1200, &loop, &QEventLoop::quit);
        loop.exec();
        qtDevices = QMediaDevices::videoInputs();
    }

    struct CamEntry { QString id, label; bool isDefault = false; };
    QList<CamEntry> devices;

    for (const auto &d : qtDevices) {
        QString id    = QString::fromUtf8(d.id());
        QString label = d.description().isEmpty() ? id
                      : QString("%1  [%2]").arg(d.description(), id);
        devices.append({id, label, false});
    }

    {
        glob_t g{};
        if (::glob("/dev/video*", GLOB_NOSORT, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) {
                QString path = QString::fromLocal8Bit(g.gl_pathv[i]);
                bool already = std::any_of(devices.begin(), devices.end(),
                                           [&](const CamEntry &e){ return e.id == path; });
                if (!already) devices.append({path, path, false});
            }
        }
        ::globfree(&g);
    }
    devices.append({"", "Default Camera  (let the system choose)", true});

    QStringList names;
    for (const auto &e : devices) names << e.label;

    bool ok = false;
    QString chosen = QInputDialog::getItem(parent, "Select Camera",
                                           "Camera device:", names, 0, false, &ok);
    if (!ok) return false;

    int idx = names.indexOf(chosen);
    const CamEntry &entry = devices[idx];

    desc.kind        = SourceDescriptor::Kind::Camera;
    desc.path        = entry.id;
    desc.displayName = entry.isDefault ? "Default Camera"
                     : entry.label.section("  [", 0, 0).trimmed();
    if (desc.displayName.isEmpty()) desc.displayName = entry.id.isEmpty() ? "Default Camera" : entry.id;
    desc.cameraIndex = 0;
    if (!entry.isDefault) {
        for (int i = 0; i < qtDevices.size(); ++i) {
            if (QString::fromUtf8(qtDevices[i].id()) == entry.id) {
                desc.cameraIndex = i;
                break;
            }
        }
    }

    thumb = ThumbHelper::makeIconThumb("📷");
    return true;
}

bool promptScreen(SourceDescriptor &desc, QPixmap &thumb) {
    desc.kind        = SourceDescriptor::Kind::Screen;
    desc.displayName = "Screen Capture";
    desc.screenIndex = 0;
    thumb = ThumbHelper::makeIconThumb("🖥");
    return true;
}

bool promptWindow(SourceDescriptor &desc, QPixmap &thumb) {
    desc.kind        = SourceDescriptor::Kind::Window;
    desc.displayName = "Window / Tab";
    desc.windowIndex = 0;
    thumb = ThumbHelper::makeIconThumb("🪟");
    return true;
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
    HtmlEditDialog dlg(QString(), parent);
    if (dlg.exec() != QDialog::Accepted) return false;
    QString filePath = dlg.resultFilePath();
    QString html     = dlg.resultHtml().trimmed();
    if (filePath.isEmpty() && html.isEmpty()) return false;

    desc.kind        = SourceDescriptor::Kind::Html;
    desc.htmlContent = html;
    desc.path        = filePath;
    desc.displayName = filePath.isEmpty() ? "HTML Overlay"
                                          : QFileInfo(filePath).fileName();
    thumb = ThumbHelper::makeHtmlThumb(html, filePath);
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
            QObject::tr("NDI is not available. Install the NDI SDK and rebuild SwitchX with -DNDI_ROOT=…"));
        return false;
    }

    QStringList sources = NdiSource::discoverSources(2000);
    if (sources.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("NDI Input"),
            QObject::tr("No NDI sources found on the network.\n\n"
               "Make sure another app (SwitchX program output, OBS, phone NDI app) is sending NDI, "
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
    thumb = ThumbHelper::makeIconThumb(QStringLiteral("📡"));
    return true;
}

#ifdef SWITCHX_HAVE_WEBRTC
bool showWebRtcConnectionDialog(QWidget *parent, const WebRtcPairingInfo &info, bool destroyOnCancel) {
    QJsonObject qrObj = WebRtcPairing::makePayload(
        info.host, info.sigPort, info.token, WebRtcManager::instance().httpPort());
    const QString qrPayload = WebRtcPairing::toQrUrl(qrObj);

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Phone Camera (WebRTC)"));
    dlg.setMinimumWidth(360);

    auto *qrLabel = new QLabel(&dlg);
    qrLabel->setAlignment(Qt::AlignCenter);
    qrLabel->setPixmap(QrCodeHelper::toPixmap(qrPayload, 5));

    auto *statusLabel = new QLabel(QObject::tr("Waiting for phone…"), &dlg);
    statusLabel->setAlignment(Qt::AlignCenter);

    auto *hintLabel = new QLabel(
        QObject::tr("Scan with your phone camera to open the browser streamer, or scan with the SwitchX app to pair natively.\n"
                    "Your phone may warn about the self-signed certificate — accept it to enable the camera."), &dlg);
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

bool promptWebRtc(QWidget *parent, SourceDescriptor &desc, QPixmap &thumb) {
    if (!WebRtcSource::isAvailable()) {
        QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
            QObject::tr("WebRTC is not available in this build.\n"
                        "Rebuild with -DSWITCHX_WITH_WEBRTC=ON and install qt6-websockets."));
        return false;
    }

    const QString bindAddress = promptWebRtcBindAddress(parent);
    if (bindAddress.isEmpty())
        return false;

    if (!ensureWebRtcFirewall(parent))
        return false;

    const WebRtcPairingInfo info = WebRtcManager::instance().createSession(bindAddress);
    if (info.token.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
            QObject::tr("Could not start the WebRTC servers on %1.\n"
                        "Check that ports %2 (signaling) and %3 (browser) are free.")
                .arg(bindAddress)
                .arg(WebRtcPairing::kDefaultSigPort)
                .arg(WebRtcPairing::kDefaultHttpPort));
        return false;
    }

    QJsonObject qrObj = WebRtcPairing::makePayload(
        info.host, info.sigPort, info.token, WebRtcManager::instance().httpPort());
    const QString qrPayload = WebRtcPairing::toQrUrl(qrObj);

    if (!showWebRtcConnectionDialog(parent, info, true))
        return false;

    desc.kind        = SourceDescriptor::Kind::WebRtc;
    desc.path        = info.token;
    desc.displayName = QObject::tr("Phone Camera");
    thumb = QrCodeHelper::toPixmap(qrPayload, 4);
    return true;
}

bool reconnectWebRtcDialog(QWidget *parent, const QString &sessionToken) {
    if (sessionToken.isEmpty())
        return false;

    if (!WebRtcSource::isAvailable()) {
        QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
            QObject::tr("WebRTC is not available in this build.\n"
                        "Rebuild with -DSWITCHX_WITH_WEBRTC=ON and install qt6-websockets."));
        return false;
    }

    QString bindAddress = WebRtcManager::instance().bindAddress();
    if (!WebRtcManager::instance().hasSession(sessionToken) || bindAddress.isEmpty()) {
        bindAddress = promptWebRtcBindAddress(parent);
        if (bindAddress.isEmpty())
            return false;
    }

    if (!ensureWebRtcFirewall(parent))
        return false;

    const WebRtcPairingInfo info = WebRtcManager::instance().ensureSession(bindAddress, sessionToken);
    if (info.token.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Phone Camera (WebRTC)"),
            QObject::tr("Could not start the WebRTC servers on %1.\n"
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

#ifdef SWITCHX_HAVE_WEBRTC
bool reconnectWebRtc(QWidget *parent, const QString &sessionToken) {
    return reconnectWebRtcDialog(parent, sessionToken);
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
        return promptScreen(outDesc, outThumb);
    case SourceDescriptor::Kind::Window:
        return promptWindow(outDesc, outThumb);
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
#ifdef SWITCHX_HAVE_WEBRTC
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
               std::function<void(SourceDescriptor::Kind)> onKind,
               bool ndiAvailable,
               bool webrtcAvailable)
{
    if (!menu) return;

    menu->addAction(QStringLiteral("🎬  Media File…"), std::move(onFile));
    menu->addAction(QStringLiteral("📁  Slideshow…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Slideshow); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("📷  Camera…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Camera); });
    menu->addAction(QStringLiteral("🖥  Screen Capture…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Screen); });
    menu->addAction(QStringLiteral("🪟  Window / Tab…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Window); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("⬜  Canvas…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Canvas); });
    menu->addAction(QStringLiteral("≋  Shader…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Shader); });
    menu->addAction(QStringLiteral("🌐  HTML Overlay…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Html); });
    menu->addAction(QStringLiteral("T  Text…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Text); });
    QAction *ndiAction = menu->addAction(QStringLiteral("📡  NDI Source…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Ndi); });
    ndiAction->setEnabled(ndiAvailable);
    if (!ndiAvailable) {
        ndiAction->setToolTip(QObject::tr(
            "NDI SDK not found at build time. Install the NDI SDK and rebuild with -DNDI_ROOT=…"));
    }
    QAction *webrtcAction = menu->addAction(QStringLiteral("📱  Phone Camera (WebRTC)…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::WebRtc); });
    webrtcAction->setEnabled(webrtcAvailable);
    if (!webrtcAvailable) {
        webrtcAction->setToolTip(QObject::tr(
            "WebRTC not enabled at build time. Rebuild with -DSWITCHX_WITH_WEBRTC=ON."));
    }
}

} // namespace SourcePrompt
