#include "TestSupport.h"
#include "core/project/AssetPathResolver.h"
#include "core/project/ProjectPackager.h"
#include "core/webrtc/NetworkUtils.h"
#include "core/sources/SourceDescriptor.h"
#include "core/project/OverlayItem.h"
#include "ui/nodes/ClipCard.h"
#include "ui/nodes/ClipNodeModel.h"

#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>
#include <QWidget>

class TestPackaging : public QObject {
    Q_OBJECT

private slots:
    void assetPathResolver_storePathEdgeCases() {
        QCOMPARE(AssetPathResolver::storePath(QString(), QDir()), QString());

        const QString relative = QStringLiteral("media/clip.png");
        QCOMPARE(AssetPathResolver::storePath(relative, QDir("/tmp/session")), relative);

        QTemporaryDir sessionRoot;
        const QDir sessionDir(sessionRoot.path());
        const QString inside = sessionDir.filePath(QStringLiteral("assets/clip.png"));
        QDir().mkpath(QFileInfo(inside).absolutePath());
        QFile file(inside);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("png");
        QCOMPARE(AssetPathResolver::storePath(inside, sessionDir), QStringLiteral("assets/clip.png"));
    }

    void assetPathResolver_resolveAbsoluteAndDirectory() {
        QTemporaryDir sessionRoot;
        const QDir sessionDir(sessionRoot.path());

        const QString slideDir = sessionDir.filePath(QStringLiteral("slides"));
        QVERIFY(QDir().mkpath(slideDir));
        TestSupport::writePng(slideDir, QStringLiteral("one.png"));

        AssetPathResolver::Options opts;
        opts.sessionDir = sessionDir;
        opts.allowUserPrompt = false;

        const QString resolvedDir = AssetPathResolver::resolvePath(
            slideDir, opts, true);
        QCOMPARE(resolvedDir, QFileInfo(slideDir).absoluteFilePath());

        const QString resolvedFile = AssetPathResolver::resolvePath(
            QDir(slideDir).filePath(QStringLiteral("one.png")), opts, false);
        QVERIFY(QFileInfo::exists(resolvedFile));
    }

    void assetPathResolver_relinkSlideshowAndMissingImage() {
        QTemporaryDir sessionRoot;
        const QDir sessionDir(sessionRoot.path());
        const QString slideDir = sessionDir.filePath(QStringLiteral("show"));
        QVERIFY(QDir().mkpath(slideDir));

        AssetPathResolver::Options opts;
        opts.sessionDir = sessionDir;
        opts.allowUserPrompt = false;

        SourceDescriptor slideshow;
        slideshow.kind = SourceDescriptor::Kind::Slideshow;
        slideshow.path = QStringLiteral("show");
        AssetPathResolver::RelinkReport report;
        const SourceDescriptor relinked = AssetPathResolver::relinkDescriptor(slideshow, opts, &report);
        QCOMPARE(relinked.path, QFileInfo(slideDir).absoluteFilePath());
        QCOMPARE(report.resolved, 1);

        SourceDescriptor missingImage;
        missingImage.kind = SourceDescriptor::Kind::Image;
        missingImage.path = QStringLiteral("missing.png");
        report = {};
        const SourceDescriptor missingOut = AssetPathResolver::relinkDescriptor(missingImage, opts, &report);
        QCOMPARE(missingOut.path, QStringLiteral("missing.png"));
        QCOMPARE(report.stillMissing, 1);

        ClipSettings settings;
        OverlayItem text;
        text.type = OverlayItem::Type::Text;
        text.content = QStringLiteral("label");
        settings.overlays.append(text);
        report = {};
        const ClipSettings textOnly = AssetPathResolver::relinkSettings(settings, opts, &report);
        QCOMPARE(textOnly.overlays.first().content, QStringLiteral("label"));
        QCOMPARE(report.resolved, 0);
        QCOMPARE(report.stillMissing, 0);
    }

    void projectPackager_exportImportRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString imagePath = dir.filePath(QStringLiteral("clip.png"));
        QVERIFY(TestSupport::writePng(dir.path(), QStringLiteral("clip.png"), Qt::blue));

        QWidget parent;
        ClipCard card(0, &parent);
        ClipNodeModel model;
        model.setCard(&card);
        model.loadClip(imagePath, QPixmap());

        QJsonObject session;
        session.insert(QStringLiteral("version"), 1);
        QJsonObject graph;
        QJsonArray nodes;
        QJsonObject node;
        QJsonObject source;
        source.insert(QStringLiteral("kind"), QStringLiteral("Image"));
        source.insert(QStringLiteral("path"), imagePath);
        node.insert(QStringLiteral("source"), source);
        node.insert(QStringLiteral("settings"), ClipSettings{}.toJson());
        nodes.append(node);
        graph.insert(QStringLiteral("clipNodes"), nodes);
        session.insert(QStringLiteral("graph"), graph);

        const QString packagePath = dir.filePath(QStringLiteral("project.prism"));
        const auto exportReport = ProjectPackager::exportPackage(session, {&model}, packagePath);
        QVERIFY2(exportReport.success, qPrintable(exportReport.error));
        QVERIFY(QFileInfo::exists(packagePath));
        QVERIFY(exportReport.assetCount >= 1);

        const QString extractDir = dir.filePath(QStringLiteral("extracted"));
        const auto importResult = ProjectPackager::importPackage(packagePath, extractDir, true);
        QVERIFY2(importResult.success, qPrintable(importResult.error));
        QVERIFY(QFileInfo::exists(importResult.sessionPath));
        QVERIFY(importResult.assetCount >= 1);

        QFile sessionFile(importResult.sessionPath);
        QVERIFY(sessionFile.open(QIODevice::ReadOnly));
        const QJsonObject imported = QJsonDocument::fromJson(sessionFile.readAll()).object();
        const QJsonArray importedNodes =
            imported.value(QStringLiteral("graph")).toObject()
                .value(QStringLiteral("clipNodes")).toArray();
        QVERIFY(!importedNodes.isEmpty());
        const QString packagedPath =
            importedNodes.first().toObject().value(QStringLiteral("source")).toObject()
                .value(QStringLiteral("path")).toString();
        QVERIFY(!QDir::isAbsolutePath(packagedPath));
        QVERIFY(QFileInfo::exists(QDir(extractDir).filePath(packagedPath)));
    }

    void projectPackager_exportEmptySession() {
        QTemporaryDir dir;
        const QJsonObject session{{QStringLiteral("version"), 1}};
        const QString packagePath = dir.filePath(QStringLiteral("empty.prism"));
        const auto report = ProjectPackager::exportPackage(session, {}, packagePath);
        QVERIFY2(report.success, qPrintable(report.error));
        QVERIFY(QFileInfo::exists(packagePath));
        QCOMPARE(report.assetCount, 0);

        const auto imported = ProjectPackager::importPackage(
            packagePath, dir.filePath(QStringLiteral("out")), true);
        QVERIFY2(imported.success, qPrintable(imported.error));
        QVERIFY(QFileInfo::exists(imported.sessionPath));
    }

    void projectPackager_importWithoutIntegrityCheck() {
        QTemporaryDir dir;
        const QString zipPath = dir.filePath(QStringLiteral("loose.prism"));
        const QByteArray asset = QByteArray("asset-data");

        QJsonArray files;
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("assets/a.bin"));
        entry.insert(QStringLiteral("size"), static_cast<qint64>(asset.size()));
        entry.insert(QStringLiteral("sha256"), QStringLiteral("deadbeef"));
        files.append(entry);

        QJsonObject manifest;
        manifest.insert(QStringLiteral("format"), QStringLiteral("prism-project"));
        manifest.insert(QStringLiteral("formatVersion"), ProjectPackager::kFormatVersion);
        manifest.insert(QStringLiteral("sessionFile"), QString::fromUtf8(ProjectPackager::kSessionName));
        manifest.insert(QStringLiteral("files"), files);

        QMap<QString, QByteArray> entries;
        entries.insert(QString::fromUtf8(ProjectPackager::kManifestName),
                       QJsonDocument(manifest).toJson(QJsonDocument::Compact));
        entries.insert(QString::fromUtf8(ProjectPackager::kSessionName), QByteArray("{}"));
        entries.insert(QStringLiteral("assets/a.bin"), asset);
        QVERIFY(TestSupport::createZip(zipPath, entries));

        const auto strict = ProjectPackager::importPackage(
            zipPath, dir.filePath(QStringLiteral("strict")), true);
        QVERIFY(!strict.success);

        const auto loose = ProjectPackager::importPackage(
            zipPath, dir.filePath(QStringLiteral("loose")), false);
        QVERIFY2(loose.success, qPrintable(loose.error));
    }

    void networkUtils_listAndLocalIpv4() {
        const auto ifaces = NetworkUtils::listIpv4Interfaces();
        for (const NetworkUtils::Ipv4Interface &iface : ifaces) {
            QVERIFY(QHostAddress(iface.address).protocol() == QAbstractSocket::IPv4Protocol);
            QVERIFY(!iface.deviceName.isEmpty());
            QVERIFY(!iface.kind.isEmpty());
            QVERIFY(iface.label.contains(iface.address));
        }

        const QString local = NetworkUtils::localIpv4();
        QVERIFY(QHostAddress(local).protocol() == QAbstractSocket::IPv4Protocol);
        if (ifaces.isEmpty()) {
            QCOMPARE(local, QStringLiteral("127.0.0.1"));
        } else {
            QCOMPARE(local, ifaces.first().address);
        }
    }

    void assetPathResolver_relinkAudioFile() {
        QTemporaryDir sessionRoot;
        const QDir sessionDir(sessionRoot.path());
        const QString audioPath = sessionDir.filePath(QStringLiteral("voice.wav"));
        QFile audioFile(audioPath);
        QVERIFY(audioFile.open(QIODevice::WriteOnly));
        audioFile.write("RIFF");
        audioFile.close();

        AssetPathResolver::Options opts;
        opts.sessionDir = sessionDir;
        opts.allowUserPrompt = false;

        SourceDescriptor audio;
        audio.kind = SourceDescriptor::Kind::AudioFile;
        audio.path = QStringLiteral("voice.wav");
        AssetPathResolver::RelinkReport report;
        const SourceDescriptor relinked = AssetPathResolver::relinkDescriptor(audio, opts, &report);
        QCOMPARE(relinked.path, QFileInfo(audioPath).absoluteFilePath());
        QCOMPARE(report.resolved, 1);
    }

    void networkUtils_defaultInterfaceIndexLive() {
        const auto ifaces = NetworkUtils::listIpv4Interfaces();
        const int idx = NetworkUtils::defaultInterfaceIndex(ifaces);
        if (ifaces.isEmpty()) {
            QCOMPARE(idx, 0);
            return;
        }
        QVERIFY(idx >= 0 && idx < ifaces.size());
    }
};

QTEST_MAIN(TestPackaging)
#include "test_packaging.moc"
