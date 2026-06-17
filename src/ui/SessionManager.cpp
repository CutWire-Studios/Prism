#include "ui/SessionManager.h"
#include "ui/ClipNodeEditor.h"
#include "ui/VideoWidget.h"
#include "ui/ThumbHelper.h"
#include "core/AssetPathResolver.h"
#include "core/ClipManager.h"
#include "core/ThumbnailExtractor.h"
#include "core/NdiSource.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QStandardPaths>
#include <QKeySequence>
#include <QDateTime>
#include <QCoreApplication>
#include <algorithm>

#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#endif

namespace {

bool isProcessAlive(qint64 pid)
{
#ifndef _WIN32
    if (pid <= 0)
        return false;
    return kill(static_cast<pid_t>(pid), 0) == 0;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

} // namespace

SessionManager::SessionManager(ClipNodeEditor *editor,
                               VideoWidget    *videoWidget,
                               ClipManager    *clipManager,
                               QWidget        *dialogParent,
                               QObject        *parent)
    : QObject(parent)
    , m_editor(editor)
    , m_videoWidget(videoWidget)
    , m_clipManager(clipManager)
    , m_dialogParent(dialogParent)
{
}

QString SessionManager::configDirectory() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir;
}

QString SessionManager::autosavePath() {
    return QDir(configDirectory()).filePath(QStringLiteral("session.sxs"));
}

QString SessionManager::lockFilePath() {
    return QDir(configDirectory()).filePath(QStringLiteral("session.lock"));
}

QString SessionManager::backupsDirectory() {
    const QString dir = QDir(configDirectory()).filePath(QStringLiteral("backups"));
    QDir().mkpath(dir);
    return dir;
}

SessionManager::RecoveryInfo SessionManager::checkRecovery() {
    RecoveryInfo info;
    info.autosavePath = autosavePath();
    info.backupPaths  = SessionManager::listBackupFiles();

    const QString lockPath = lockFilePath();
    if (!QFile::exists(lockPath))
        return info;

    QFile lockFile(lockPath);
    if (lockFile.open(QIODevice::ReadOnly)) {
        const QJsonObject lockObj = QJsonDocument::fromJson(lockFile.readAll()).object();
        const qint64 pid = lockObj.value(QStringLiteral("pid")).toInteger();
        if (isProcessAlive(pid))
            return info;
    }

    info.uncleanShutdown = true;
    return info;
}

void SessionManager::markRunning() {
    QJsonObject lockObj;
    lockObj.insert(QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid()));
    lockObj.insert(QStringLiteral("startedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));

    QFile lockFile(lockFilePath());
    if (lockFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        lockFile.write(QJsonDocument(lockObj).toJson(QJsonDocument::Compact));
}

void SessionManager::markCleanExit() {
    QFile::remove(lockFilePath());
}

bool SessionManager::writeSessionFile(const QJsonObject &json, const QString &path) const {
    const QString tempPath = path + QStringLiteral(".tmp");
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly))
        return false;

    tempFile.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    tempFile.close();

    if (QFile::exists(path) && !QFile::remove(path))
        return false;

    return QFile::rename(tempPath, path);
}

QStringList SessionManager::listBackupFiles() {
    QDir dir(backupsDirectory());
    QStringList files = dir.entryList({QStringLiteral("session-*.sxs")},
                                      QDir::Files, QDir::Name);
    std::sort(files.begin(), files.end(), std::greater<QString>());
    QStringList paths;
    paths.reserve(files.size());
    for (const QString &name : files)
        paths << dir.absoluteFilePath(name);
    return paths;
}

void SessionManager::pruneBackups(int keepCount) {
    if (keepCount < 0)
        keepCount = 0;

    const QStringList backups = listBackupFiles();
    for (int i = keepCount; i < backups.size(); ++i)
        QFile::remove(backups.at(i));
}

bool SessionManager::saveAutosave(const QJsonObject &json, int backupRetention) {
    QJsonObject enriched = json;
    enriched.insert(QStringLiteral("savedAt"),
                    QDateTime::currentDateTime().toString(Qt::ISODate));

    const QString primary = autosavePath();
    if (!writeSessionFile(enriched, primary))
        return false;

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const QString backupPath = QDir(backupsDirectory()).filePath(
        QStringLiteral("session-%1.sxs").arg(stamp));
    writeSessionFile(enriched, backupPath);

    pruneBackups(backupRetention);
    return true;
}

QJsonObject SessionManager::buildJson(int crossfader, int transitionMode,
                                      double transitionDuration,
                                      NodeId activeNodeA, NodeId activeNodeB,
                                      const QMap<NodeId, Qt::Key> &nodeHotkeys,
                                      const QString &sessionFilePath) const {
    QJsonObject root;
    root["version"]            = 1;
    root["crossfader"]         = crossfader;
    root["transitionMode"]     = transitionMode;
    root["transitionDuration"] = transitionDuration;
    root["activeNodeA"]        = (qint64)activeNodeA;
    root["activeNodeB"]        = (qint64)activeNodeB;

    const QDir sessionDir = sessionFilePath.isEmpty()
        ? QDir()
        : QDir(QFileInfo(sessionFilePath).absolutePath());
    if (sessionDir.isAbsolute())
        root["sessionDir"] = sessionDir.absolutePath();

    QJsonArray hotkeys;
    for (auto it = nodeHotkeys.cbegin(); it != nodeHotkeys.cend(); ++it) {
        QJsonObject hk;
        hk["nodeId"] = (qint64)it.key();
        hk["key"]    = (int)it.value();
        hotkeys.append(hk);
    }
    root["hotkeys"] = hotkeys;
    root["graph"]   = m_editor->saveState(sessionDir);
    return root;
}

bool SessionManager::loadFromFile(const QString &path, bool showErrors) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (showErrors)
            QMessageBox::warning(m_dialogParent, "Load Session",
                                 QString("Cannot open file:\n%1").arg(path));
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (doc.isNull()) {
        if (showErrors)
            QMessageBox::warning(m_dialogParent, "Load Session",
                                 QString("Invalid session file:\n%1").arg(err.errorString()));
        return false;
    }
    const QJsonObject root = doc.object();
    if (root["version"].toInt() != 1) {
        if (showErrors)
            QMessageBox::warning(m_dialogParent, "Load Session",
                                 "Unsupported session version.");
        return false;
    }

    // Tear down current state ──────────────────────────────────────────────────
    m_clipManager->clear();
    m_videoWidget->setSourceA(nullptr);
    m_videoWidget->setSourceB(nullptr);
    m_videoWidget->setNodeChainA({});
    m_videoWidget->setNodeChainB({});

    // Restore graph ──────────────────────────────────────────────────────────
    m_editor->restoreState(root["graph"].toObject());

    AssetPathResolver::Options relinkOpts;
    relinkOpts.dialogParent = m_dialogParent;
    relinkOpts.allowUserPrompt = showErrors;
    const QString savedSessionDir = root["sessionDir"].toString();
    if (!savedSessionDir.isEmpty())
        relinkOpts.sessionDir = QDir(savedSessionDir);
    else
        relinkOpts.sessionDir = QDir(QFileInfo(path).absolutePath());

    AssetPathResolver::RelinkReport relinkReport;

    // Re-generate thumbnails for every restored node ─────────────────────────
    for (ClipNodeModel *model : m_editor->allNodes()) {
        SourceDescriptor desc = AssetPathResolver::relinkDescriptor(
            model->sourceDescriptor(), relinkOpts, &relinkReport);
        ClipSettings settings = AssetPathResolver::relinkSettings(
            model->settings(), relinkOpts, &relinkReport);

        using Kind = SourceDescriptor::Kind;
        QPixmap thumb;
        switch (desc.kind) {
        case Kind::VideoFile:
        case Kind::Image:
            thumb = ThumbnailExtractor::extract(desc.path, 110, 65);
            break;
        case Kind::Slideshow: {
            QDir dir(desc.path);
            QStringList imgs = dir.entryList({"*.png","*.jpg","*.jpeg","*.bmp","*.webp"},
                                              QDir::Files, QDir::Name);
            if (!imgs.isEmpty())
                thumb = ThumbnailExtractor::extract(dir.absoluteFilePath(imgs.first()), 110, 65);
            if (thumb.isNull()) thumb = ThumbHelper::makeIconThumb("📁");
            break;
        }
        case Kind::Camera: thumb = ThumbHelper::makeIconThumb("📷"); break;
        case Kind::Screen: thumb = ThumbHelper::makeIconThumb("🖥");  break;
        case Kind::Window: thumb = ThumbHelper::makeIconThumb("🪟");  break;
        case Kind::Canvas:
            thumb = ThumbHelper::makeCanvasThumb(
                QString("%1x%2").arg(desc.canvasWidth).arg(desc.canvasHeight),
                desc.canvasFill, desc.color);
            break;
        case Kind::Shader: thumb = ThumbHelper::makeShaderThumb(desc.shaderCode);           break;
        case Kind::Html:   thumb = ThumbHelper::makeHtmlThumb(desc.htmlContent, desc.path); break;
        case Kind::Ndi:    thumb = ThumbHelper::makeIconThumb(QStringLiteral("📡"));       break;
        }
        if (!thumb.isNull()) {
            if (desc.kind == Kind::VideoFile || desc.kind == Kind::Image)
                model->loadClip(desc.path, thumb);
            else
                model->loadSource(desc, thumb);
            model->applySettings(settings);
        }
    }

    if (showErrors && relinkReport.stillMissing > 0) {
        QMessageBox::warning(
            m_dialogParent, "Load Session",
            QString("%1 asset(s) could not be found:\n\n%2")
                .arg(relinkReport.stillMissing)
                .arg(relinkReport.notes.join('\n')));
    }

    // Parse restored hotkeys ─────────────────────────────────────────────────
    m_restoredHotkeys.clear();
    const QJsonArray hotkeys = root["hotkeys"].toArray();
    for (const auto &hkVal : hotkeys) {
        const QJsonObject hk  = hkVal.toObject();
        const NodeId nodeId   = (NodeId)hk["nodeId"].toInteger();
        const Qt::Key key     = (Qt::Key)hk["key"].toInt();
        m_restoredHotkeys[key] = nodeId;
    }

    // Restore UI values ───────────────────────────────────────────────────────
    m_restoredCrossfader         = root["crossfader"].toInt(50);
    m_restoredTransitionMode     = root["transitionMode"].toInt(0);
    m_restoredTransitionDuration = root["transitionDuration"].toDouble(1.0);
    m_restoredActiveNodeA        = (NodeId)root["activeNodeA"].toInteger();
    m_restoredActiveNodeB        = (NodeId)root["activeNodeB"].toInteger();

    emit sessionLoaded();
    return true;
}
