#include "app/session/SessionManager.h"
#include "core/project/AssetPathResolver.h"
#include "core/project/ClipManager.h"
#include "core/render/Compositor.h"
#include "core/sources/SvgTemplates.h"
#include "core/sources/NdiSource.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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

SessionManager::SessionManager(ClipGraph *graph, Compositor *compositor,
                               ClipManager *clipManager, QObject *parent)
    : QObject(parent)
    , m_graph(graph)
    , m_compositor(compositor)
    , m_clipManager(clipManager)
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

void SessionManager::relativizeGraphPaths(QJsonObject &graph, const QDir &sessionDir) {
    QJsonArray nodes = graph["inputNodes"].toArray();
    for (int n = 0; n < nodes.size(); ++n) {
        QJsonObject node = nodes.at(n).toObject();

        QJsonObject source = node["source"].toObject();
        source["path"] = AssetPathResolver::storePath(source["path"].toString(), sessionDir);
        const QString svgId = source["svgTemplateId"].toString();
        if (!prism::isBuiltinSvgTemplate(svgId))
            source["svgTemplateId"] = AssetPathResolver::storePath(svgId, sessionDir);
        node["source"] = source;

        QJsonObject settings = node["settings"].toObject();
        QJsonArray overlays = settings["overlays"].toArray();
        for (int i = 0; i < overlays.size(); ++i) {
            QJsonObject ov = overlays.at(i).toObject();
            if (ov["type"].toString() == QLatin1String("image")) {
                ov["content"] = AssetPathResolver::storePath(ov["content"].toString(), sessionDir);
                overlays[i] = ov;
            }
        }
        settings["overlays"] = overlays;
        node["settings"] = settings;

        nodes[n] = node;
    }
    graph["inputNodes"] = nodes;
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
    root["outputResolution"]   = QJsonObject{{"w", Compositor::programWidth()},
                                             {"h", Compositor::programHeight()}};
    switch (Compositor::outputScaleMode()) {
    case Compositor::OutputScaleMode::Stretch: root["outputScaleMode"] = "stretch"; break;
    case Compositor::OutputScaleMode::Fit:     root["outputScaleMode"] = "fit";     break;
    case Compositor::OutputScaleMode::Crop:    root["outputScaleMode"] = "crop";    break;
    }

    const QDir sessionDir = sessionFilePath.isEmpty()
        ? QDir()
        : QDir(QFileInfo(sessionFilePath).absolutePath());
    if (sessionDir.isAbsolute())
        root["sessionDir"] = sessionDir.absolutePath();

    root["hotkeys"] = hotkeys;
    QJsonObject graph = m_graph->toJson();
    if (sessionDir.isAbsolute())
        relativizeGraphPaths(graph, sessionDir);
    root["graph"]   = graph;

    QJsonArray assetLibrary;
    for (const QString &path : m_clipManager->getClips()) {
        assetLibrary.append(sessionDir.isAbsolute()
            ? AssetPathResolver::storePath(path, sessionDir)
            : path);
    }
    root["assetLibrary"] = assetLibrary;

    QJsonArray assetFolders;
    for (const ClipManager::BinFolder &f : m_clipManager->folders()) {
        QJsonObject o;
        o["id"]     = f.id;
        o["name"]   = f.name;
        o["parent"] = f.parentId;
        assetFolders.append(o);
    }
    root["assetFolders"] = assetFolders;

    QJsonObject assetClipFolders;
    for (const QString &path : m_clipManager->getClips()) {
        const QString folderId = m_clipManager->folderOf(path);
        if (folderId.isEmpty())
            continue;
        assetClipFolders[sessionDir.isAbsolute()
            ? AssetPathResolver::storePath(path, sessionDir)
            : path] = folderId;
    }
    root["assetClipFolders"] = assetClipFolders;
    return root;
}

bool SessionManager::loadFromFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit loadFailed(QStringLiteral("Cannot open file:\n%1").arg(path));
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (doc.isNull()) {
        emit loadFailed(QStringLiteral("Invalid session file:\n%1").arg(err.errorString()));
        return false;
    }
    const QJsonObject root = doc.object();
    if (root["version"].toInt() != 1) {
        emit loadFailed(QStringLiteral("Unsupported session version."));
        return false;
    }

    m_clipManager->clear();
    m_compositor->setSourceA(nullptr);
    m_compositor->setSourceB(nullptr);
    m_compositor->setNodeChainA({});
    m_compositor->setNodeChainB({});

    m_graph->fromJson(root["graph"].toObject());

    AssetPathResolver::Options relinkOpts;
    const QString savedSessionDir = root["sessionDir"].toString();
    if (!savedSessionDir.isEmpty())
        relinkOpts.sessionDir = QDir(savedSessionDir);
    else
        relinkOpts.sessionDir = QDir(QFileInfo(path).absolutePath());

    AssetPathResolver::RelinkReport relinkReport;

    QJsonArray libArr = root["assetLibrary"].toArray();
    QStringList libPaths;
    QHash<QString, QString> resolvedByStored;
    for (const QJsonValue &v : libArr) {
        const QString stored = v.toString();
        if (stored.isEmpty())
            continue;
        const QString resolved = AssetPathResolver::resolvePath(stored, relinkOpts, false);
        if (!resolved.isEmpty()) {
            libPaths << resolved;
            resolvedByStored.insert(stored, resolved);
        }
    }
    for (const QJsonValue &v : root["assetFolders"].toArray()) {
        const QJsonObject o = v.toObject();
        m_clipManager->restoreFolder({o["id"].toString(), o["name"].toString(),
                                      o["parent"].toString()});
    }
    if (!libPaths.isEmpty())
        m_clipManager->addFiles(libPaths);
    const QJsonObject clipFolders = root["assetClipFolders"].toObject();
    for (auto it = clipFolders.begin(); it != clipFolders.end(); ++it) {
        const QString resolved = resolvedByStored.value(it.key());
        if (!resolved.isEmpty())
            m_clipManager->moveClip(resolved, it.value().toString());
    }

    for (const NodeId id : m_graph->nodeIdsOfType(ClipGraph::NodeType::Input)) {
        m_graph->setSourceDescriptor(id, AssetPathResolver::relinkDescriptor(
            m_graph->sourceDescriptor(id), relinkOpts, &relinkReport));
        m_graph->setClipSettings(id, AssetPathResolver::relinkSettings(
            m_graph->clipSettings(id), relinkOpts, &relinkReport));
    }

    if (relinkReport.stillMissing > 0)
        emit assetsMissing(relinkReport.stillMissing, relinkReport.notes.join('\n'));

    // Restored hotkeys are passed through as-is; HotkeyManager understands both
    // the current {abNode, slot, key} entries and legacy {nodeId, key} ones.
    m_restoredHotkeys = root["hotkeys"].toArray();

    // Restore UI values ───────────────────────────────────────────────────────
    m_restoredCrossfader         = root["crossfader"].toInt(50);
    m_restoredTransitionMode     = root["transitionMode"].toInt(0);
    m_restoredTransitionDuration = root["transitionDuration"].toDouble(1.0);
    m_restoredActiveNodeA        = (NodeId)root["activeNodeA"].toInteger();
    m_restoredActiveNodeB        = (NodeId)root["activeNodeB"].toInteger();

    const QJsonObject res = root["outputResolution"].toObject();
    const int resW = res["w"].toInt();
    const int resH = res["h"].toInt();
    if (resW >= 160 && resH >= 90)
        m_compositor->setProgramResolution(resW, resH);
    const QString scaleMode = root["outputScaleMode"].toString();
    if (scaleMode == "stretch")   m_compositor->setOutputScaleMode(Compositor::OutputScaleMode::Stretch);
    else if (scaleMode == "fit")  m_compositor->setOutputScaleMode(Compositor::OutputScaleMode::Fit);
    else if (scaleMode == "crop") m_compositor->setOutputScaleMode(Compositor::OutputScaleMode::Crop);

    emit sessionLoaded();
    return true;
}
