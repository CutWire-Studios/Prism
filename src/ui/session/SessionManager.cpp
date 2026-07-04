#include "ui/session/SessionManager.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/canvas/VideoWidget.h"
#include "ui/common/ThumbHelper.h"
#include "ui/common/MaterialSymbols.h"
#include "core/project/AssetPathResolver.h"
#include "core/project/ClipManager.h"
#include "core/media/ThumbnailExtractor.h"
#include "core/sources/NdiSource.h"
#include "core/sources/HtmlWorkspace.h"
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
#else
#define NOMINMAX
#include <windows.h>
#endif

namespace {

bool isProcessAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
#ifndef _WIN32
    return kill(static_cast<pid_t>(pid), 0) == 0;
#else
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                static_cast<DWORD>(pid));
    if (!handle)
        return false;
    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(handle, &exitCode)
                    && exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
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
    return QDir(configDirectory()).filePath(QString::fromUtf8(kAutosaveFileName));
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
    QStringList files = dir.entryList(
        {QStringLiteral("session-*") + QString::fromUtf8(kSessionExtension)},
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
        QStringLiteral("session-%1%2").arg(stamp, QString::fromUtf8(kSessionExtension)));
    writeSessionFile(enriched, backupPath);

    pruneBackups(backupRetention);
    return true;
}

QJsonObject SessionManager::buildJson(int crossfader, int transitionMode,
                                      double transitionDuration,
                                      NodeId activeNodeA, NodeId activeNodeB,
                                      const QJsonArray &hotkeys,
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

    root["hotkeys"] = hotkeys;
    root["graph"]   = m_editor->saveState(sessionDir);

    QJsonArray assetLibrary;
    for (const QString &path : m_clipManager->getClips()) {
        assetLibrary.append(sessionDir.isAbsolute()
            ? AssetPathResolver::storePath(path, sessionDir)
            : path);
    }
    root["assetLibrary"] = assetLibrary;
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

    QJsonArray libArr = root["assetLibrary"].toArray();
    QStringList libPaths;
    for (const QJsonValue &v : libArr) {
        const QString stored = v.toString();
        if (stored.isEmpty())
            continue;
        const QString resolved = AssetPathResolver::resolvePath(stored, relinkOpts, false);
        if (!resolved.isEmpty())
            libPaths << resolved;
    }
    if (!libPaths.isEmpty())
        m_clipManager->addFiles(libPaths);

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
            if (thumb.isNull()) thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::Folder);
            break;
        }
        case Kind::Camera: thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::PhotoCamera); break;
        case Kind::Screen: thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::DesktopWindows); break;
        case Kind::Window: thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::SelectWindow); break;
        case Kind::Canvas:
            thumb = ThumbHelper::makeCanvasThumb(
                QString("%1x%2").arg(desc.canvasWidth).arg(desc.canvasHeight),
                desc.canvasFill, desc.color);
            break;
        case Kind::Shader: thumb = ThumbHelper::makeShaderThumb(desc.shaderCode);           break;
        case Kind::Html: {
            QString html = desc.htmlContent;
            QString path = desc.path;
            if (!desc.htmlWorkspace.isEmpty()) {
                html = HtmlWorkspaceBuilder::buildFromJson(desc.htmlWorkspace);
                path = {};
            }
            thumb = ThumbHelper::makeHtmlThumb(html, path);
            break;
        }
        case Kind::Text:   thumb = ThumbHelper::makeTextThumb(desc.textTemplate, desc.color); break;
        case Kind::Ndi:    thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::Sensors); break;
        case Kind::WebRtc: thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::Smartphone); break;
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

    // Restored hotkeys are passed through as-is; HotkeyManager understands both
    // the current {abNode, slot, key} entries and legacy {nodeId, key} ones.
    m_restoredHotkeys = root["hotkeys"].toArray();

    // Restore UI values ───────────────────────────────────────────────────────
    m_restoredCrossfader         = root["crossfader"].toInt(50);
    m_restoredTransitionMode     = root["transitionMode"].toInt(0);
    m_restoredTransitionDuration = root["transitionDuration"].toDouble(1.0);
    m_restoredActiveNodeA        = (NodeId)root["activeNodeA"].toInteger();
    m_restoredActiveNodeB        = (NodeId)root["activeNodeB"].toInteger();

    emit sessionLoaded();
    return true;
}
