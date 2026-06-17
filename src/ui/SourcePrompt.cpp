#include "ui/SourcePrompt.h"
#include "ui/ThumbHelper.h"
#include "ui/ShaderEditDialog.h"
#include "ui/HtmlEditDialog.h"
#include "core/ThumbnailExtractor.h"
#include "core/NdiSource.h"

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

    struct CamEntry { QString id, label; bool isDefault; };
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

} // namespace

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
    case SourceDescriptor::Kind::Ndi:
        return promptNdi(parent, outDesc, outThumb);
    default:
        return false;
    }
}

void buildMenu(QMenu *menu,
               std::function<void()> onFile,
               std::function<void(SourceDescriptor::Kind)> onKind,
               bool ndiAvailable)
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
    QAction *ndiAction = menu->addAction(QStringLiteral("📡  NDI Source…"),
                    [onKind]() { onKind(SourceDescriptor::Kind::Ndi); });
    ndiAction->setEnabled(ndiAvailable);
    if (!ndiAvailable) {
        ndiAction->setToolTip(QObject::tr(
            "NDI SDK not found at build time. Install the NDI SDK and rebuild with -DNDI_ROOT=…"));
    }
}

} // namespace SourcePrompt
