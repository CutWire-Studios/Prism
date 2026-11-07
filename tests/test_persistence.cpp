#include "TestSupport.h"
#include "core/project/AssetPathResolver.h"
#include "core/project/ProjectPackager.h"
#include "core/project/ClipSettings.h"
#include "core/project/ClipManager.h"
#include "core/sources/SourceDescriptor.h"
#include "core/webrtc/NetworkUtils.h"
#include "app/session/SessionManager.h"
#include "app/show/ShowManager.h"
#include "core/graph/ClipGraph.h"

#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QFile>

namespace {

QByteArray makeManifestJson(const QJsonObject &extra = {}) {
    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QStringLiteral("prism-project"));
    manifest.insert(QStringLiteral("formatVersion"), ProjectPackager::kFormatVersion);
    manifest.insert(QStringLiteral("sessionFile"), QString::fromUtf8(ProjectPackager::kSessionName));
    manifest.insert(QStringLiteral("files"), QJsonArray{});
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        manifest.insert(it.key(), it.value());
    }
    return QJsonDocument(manifest).toJson(QJsonDocument::Compact);
}

} // namespace

class TestPersistence : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void assetPathResolver_paths() {
        QTemporaryDir sessionRoot;
        QVERIFY(sessionRoot.isValid());
        const QDir sessionDir(sessionRoot.path());

        QTemporaryDir outside;
        const QString outsideFile = outside.filePath(QStringLiteral("ext.png"));
        QVERIFY(TestSupport::writePng(outside.path(), QStringLiteral("ext.png")));

        const QString insideFile = sessionDir.filePath(QStringLiteral("media/clip.png"));
        QDir().mkpath(QFileInfo(insideFile).absolutePath());
        QVERIFY(QFile::copy(outsideFile, insideFile));

        const QString storedInside = AssetPathResolver::storePath(insideFile, sessionDir);
        QVERIFY(!QDir::isAbsolutePath(storedInside));

        const QString storedOutside = AssetPathResolver::storePath(outsideFile, sessionDir);
        QCOMPARE(storedOutside, QFileInfo(outsideFile).absoluteFilePath());

        AssetPathResolver::Options opts;
        opts.sessionDir = sessionDir;

        QCOMPARE(AssetPathResolver::resolvePath(storedInside, opts, false),
                 QFileInfo(insideFile).absoluteFilePath());

        const QString missing = QStringLiteral("missing/deep/file.mp4");
        QCOMPARE(AssetPathResolver::resolvePath(missing, opts, false), missing);

        const QString byBase = AssetPathResolver::resolvePath(QStringLiteral("clip.png"), opts, false);
        QCOMPARE(byBase, QFileInfo(insideFile).absoluteFilePath());
    }

    void assetPathResolver_relink() {
        QTemporaryDir sessionRoot;
        const QDir sessionDir(sessionRoot.path());
        const QString videoPath = sessionDir.filePath(QStringLiteral("clip.mp4"));
        QVERIFY(QFile(videoPath).open(QIODevice::WriteOnly));

        AssetPathResolver::Options opts;
        opts.sessionDir = sessionDir;

        SourceDescriptor video;
        video.kind = SourceDescriptor::Kind::VideoFile;
        video.path = QStringLiteral("clip.mp4");
        AssetPathResolver::RelinkReport report;
        const SourceDescriptor relinked = AssetPathResolver::relinkDescriptor(video, opts, &report);
        QCOMPARE(relinked.path, QFileInfo(videoPath).absoluteFilePath());
        QCOMPARE(report.resolved, 1);

        SourceDescriptor cam;
        cam.kind = SourceDescriptor::Kind::Camera;
        cam.path = QStringLiteral("/dev/video0");
        const SourceDescriptor camOut = AssetPathResolver::relinkDescriptor(cam, opts);
        QCOMPARE(camOut.path, cam.path);
    }

    void projectPackager_importErrors() {
        QTemporaryDir dir;
        const QString extractBase = dir.filePath(QStringLiteral("extract"));
        const QString zipPath = dir.filePath(QStringLiteral("pkg.prism"));
        int caseIndex = 0;

        auto runImport = [&](const QMap<QString, QByteArray> &entries) -> ProjectPackager::ImportResult {
            const QString extract = extractBase + QString::number(caseIndex++);
            if (!TestSupport::createZip(zipPath, entries)) {
                return {};
            }
            return ProjectPackager::importPackage(zipPath, extract, true);
        };

        {
            const auto result = ProjectPackager::importPackage(zipPath, extractBase, true);
            QVERIFY(!result.success);
            QVERIFY(!result.error.isEmpty());
        }

        {
            QMap<QString, QByteArray> entries;
            entries.insert(QStringLiteral("readme.txt"), QByteArray("hello"));
            const auto result = runImport(entries);
            QVERIFY(!result.success);
            QCOMPARE(result.error, QStringLiteral("Missing manifest in package."));
        }

        {
            QMap<QString, QByteArray> entries;
            entries.insert(QString::fromUtf8(ProjectPackager::kManifestName), QByteArray("{"));
            const auto result = runImport(entries);
            QVERIFY(!result.success);
            QVERIFY(result.error.startsWith(QStringLiteral("Invalid manifest:")));
        }

        {
            QMap<QString, QByteArray> entries;
            QJsonObject bad;
            bad.insert(QStringLiteral("format"), QStringLiteral("other"));
            bad.insert(QStringLiteral("formatVersion"), 1);
            entries.insert(QString::fromUtf8(ProjectPackager::kManifestName),
                           QJsonDocument(bad).toJson());
            const auto result = runImport(entries);
            QVERIFY(!result.success);
            QCOMPARE(result.error, QStringLiteral("Unsupported package format."));
        }

        {
            QMap<QString, QByteArray> entries;
            QJsonObject bad;
            bad.insert(QStringLiteral("format"), QStringLiteral("prism-project"));
            bad.insert(QStringLiteral("formatVersion"), 999);
            entries.insert(QString::fromUtf8(ProjectPackager::kManifestName),
                           QJsonDocument(bad).toJson());
            const auto result = runImport(entries);
            QVERIFY(!result.success);
            QCOMPARE(result.error, QStringLiteral("This project was saved by an older version of Prism and cannot be opened."));
        }

        {
            QMap<QString, QByteArray> entries;
            entries.insert(QString::fromUtf8(ProjectPackager::kManifestName), makeManifestJson());
            const auto result = runImport(entries);
            QVERIFY(!result.success);
            QCOMPARE(result.error, QStringLiteral("Missing session file in package."));
        }

        {
            const QByteArray asset = QByteArray("asset-bytes");
            QJsonArray files;
            QJsonObject entry;
            entry.insert(QStringLiteral("path"), QStringLiteral("assets/a.bin"));
            entry.insert(QStringLiteral("size"), static_cast<qint64>(asset.size()));
            entry.insert(QStringLiteral("sha256"), QStringLiteral("deadbeef"));
            files.append(entry);

            QJsonObject extra;
            extra.insert(QStringLiteral("files"), files);

            QMap<QString, QByteArray> entries;
            entries.insert(QString::fromUtf8(ProjectPackager::kManifestName), makeManifestJson(extra));
            entries.insert(QString::fromUtf8(ProjectPackager::kSessionName), QByteArray("{}"));
            entries.insert(QStringLiteral("assets/a.bin"), asset);
            const auto result = runImport(entries);
            QVERIFY(!result.success);
            QVERIFY(result.error.contains(QStringLiteral("Checksum mismatch")));
        }
    }

    void clipSettings_json() {
        ClipSettings settings;
        settings.startTime = 1.5;
        settings.endTime = 10.0;
        settings.cropX = 0.1f;
        settings.cropW = 0.8f;
        settings.speed = 2.5;

        const QJsonObject json = settings.toJson();
        const ClipSettings restored = ClipSettings::fromJson(json);
        QCOMPARE(restored.startTime, settings.startTime);
        QCOMPARE(restored.endTime, settings.endTime);
        QCOMPARE(restored.speed, 2.5);
        QVERIFY(restored.hasCrop());

        const ClipSettings defaults = ClipSettings::fromJson(QJsonObject{});
        QCOMPARE(defaults.startTime, 0.0);
        QCOMPARE(defaults.endTime, -1.0);
        QCOMPARE(defaults.speed, 1.0);
        QVERIFY(!defaults.hasCrop());
    }

    void clipManager_folderOps() {
        QTemporaryDir dir;
        QVERIFY(TestSupport::writePng(dir.path(), QStringLiteral("a.png")));
        QVERIFY(TestSupport::writePng(dir.path(), QStringLiteral("b.png")));
        QFile f(dir.filePath(QStringLiteral("notes.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        QVERIFY(QFile::copy(dir.filePath(QStringLiteral("a.png")), dir.filePath(QStringLiteral("a_dup.png"))));

        ClipManager mgr;
        mgr.loadFolder(dir.path());
        QVERIFY(mgr.getClipCount() >= 2);
        QVERIFY(mgr.getClipPath(0).endsWith(QStringLiteral(".png")));

        const int before = mgr.getClipCount();
        mgr.addFolder(dir.path());
        QCOMPARE(mgr.getClipCount(), before);

        mgr.addFiles({dir.filePath(QStringLiteral("notes.txt"))});
        QVERIFY(mgr.getClipPath(-1).isEmpty());
        mgr.removeClip(9999);
        mgr.removeClip(0);
        QVERIFY(mgr.getClipCount() >= 0);
    }

    void sessionWithOldFormatVersionIsRejected() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("old.psm"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QJsonObject root;
        root.insert(QStringLiteral("version"), SessionManager::kFormatVersion - 1);
        file.write(QJsonDocument(root).toJson());
        file.close();

        ShowManager show;
        ClipManager clips;
        SessionManager session(&show, nullptr, &clips);
        QSignalSpy failed(&session, &SessionManager::loadFailed);
        QVERIFY(!session.loadFromFile(path));
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("older version")));
    }

    void v3SessionWithOldKeysIsRefused() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("v3.psm"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QJsonObject root{{QStringLiteral("version"), 3},
                               {QStringLiteral("cues"), QJsonArray{}},
                               {QStringLiteral("liveCueId"), QStringLiteral("a")}};
        file.write(QJsonDocument(root).toJson());
        file.close();

        ShowManager show;
        ClipManager clips;
        SessionManager session(&show, nullptr, &clips);
        QSignalSpy failed(&session, &SessionManager::loadFailed);
        QVERIFY(!session.loadFromFile(path));
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("older version")));
    }

    void sessionJsonIsShowShaped() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ShowManager show;
        Scene *second = show.addScene(QStringLiteral("Second"));
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::Image;
        desc.path = dir.filePath(QStringLiteral("media/pic.png"));
        QVERIFY(second->graph()->addInputNode(desc) != 0);
        show.stageScene(second->id());

        ClipManager clips;
        SessionManager session(&show, nullptr, &clips);
        const QString sessionFile = dir.filePath(QStringLiteral("show.psm"));
        const QJsonObject root = session.buildJson(0, {}, sessionFile);

        QCOMPARE(root["version"].toInt(), 4);
        QVERIFY(!root.contains(QStringLiteral("graph")));
        QVERIFY(!root.contains(QStringLiteral("activeNodeLive")));
        QVERIFY(!root.contains(QStringLiteral("outputResolution")));
        QVERIFY(!root.contains(QStringLiteral("outputScaleMode")));
        QCOMPARE(root["show"].toObject()["state"].toObject()["stagingSceneId"].toString(), second->id());
        const QJsonArray scenes = root["scenes"].toArray();
        QCOMPARE(scenes.size(), 2);
        const QJsonObject nodeSource = scenes.at(1).toObject()["graph"].toObject()["inputNodes"].toArray()
                                           .first().toObject()["source"].toObject();
        QCOMPARE(nodeSource["path"].toString(), QStringLiteral("media/pic.png"));
    }

    void sceneSourceNodeRoundTrips() {
        ShowManager show;
        Scene *outer = show.liveScene();
        Scene *inner = show.addScene(QStringLiteral("Inner"));
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::Scene;
        desc.sceneId = inner->id();
        desc.displayName = QStringLiteral("Inner");
        const NodeId node = outer->graph()->addInputNode(desc);
        QVERIFY(node != 0);

        ShowManager loaded;
        QVERIFY(loaded.fromJson(show.toJson()));
        const SourceDescriptor back = loaded.scene(outer->id())->graph()->sourceDescriptor(node);
        QCOMPARE(back.kind, SourceDescriptor::Kind::Scene);
        QCOMPARE(back.sceneId, inner->id());
        QCOMPARE(back.displayName, QStringLiteral("Inner"));
    }

    void sessionManager_recoveryAndBackups() {
        SessionManager::markCleanExit();
        if (QFile::exists(SessionManager::lockFilePath())) {
            QFile::remove(SessionManager::lockFilePath());
        }

        SessionManager::markRunning();
        auto running = SessionManager::checkRecovery();
        QVERIFY(!running.uncleanShutdown);

        SessionManager::markCleanExit();
        auto clean = SessionManager::checkRecovery();
        QVERIFY(!clean.uncleanShutdown);

        {
            QFile lock(SessionManager::lockFilePath());
            QVERIFY(lock.open(QIODevice::WriteOnly | QIODevice::Truncate));
            lock.write("not json");
        }
        auto malformed = SessionManager::checkRecovery();
        QVERIFY(malformed.uncleanShutdown);

        SessionManager::markCleanExit();
        {
            QJsonObject lockObj;
            lockObj.insert(QStringLiteral("pid"), static_cast<qint64>(999999999));
            QFile lock(SessionManager::lockFilePath());
            QVERIFY(lock.open(QIODevice::WriteOnly | QIODevice::Truncate));
            lock.write(QJsonDocument(lockObj).toJson());
        }
        auto unclean = SessionManager::checkRecovery();
        QVERIFY(unclean.uncleanShutdown);
        SessionManager::markCleanExit();

        const QDir backupDir(SessionManager::backupsDirectory());
        for (int i = 0; i < 5; ++i) {
            QFile f(backupDir.filePath(
                QStringLiteral("session-%1%2").arg(i, 2, 10, QChar('0'))
                    .arg(QString::fromUtf8(SessionManager::kSessionExtension))));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        SessionManager::pruneBackups(2);
        QCOMPARE(SessionManager::listBackupFiles().size(), 2);
    }

    void networkUtils_defaultInterfaceIndex() {
        QCOMPARE(NetworkUtils::defaultInterfaceIndex({}), 0);

        NetworkUtils::Ipv4Interface wifi;
        wifi.kind = QStringLiteral("Ethernet");
        wifi.address = QStringLiteral("10.0.0.2");

        NetworkUtils::Ipv4Interface eth;
        eth.kind = QStringLiteral("Ethernet");
        eth.address = QStringLiteral("10.0.0.3");

        NetworkUtils::Ipv4Interface wifi2;
        wifi2.kind = QStringLiteral("Wi-Fi");
        wifi2.address = QStringLiteral("192.168.1.5");

        QCOMPARE(NetworkUtils::defaultInterfaceIndex({wifi, eth}), 0);
        QCOMPARE(NetworkUtils::defaultInterfaceIndex({eth, wifi2}), 1);
    }
};

QTEST_MAIN(TestPersistence)
#include "test_persistence.moc"
