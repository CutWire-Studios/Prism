#include "app/remote/RemoteProtocol.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class TestRemoteControl : public QObject {
    Q_OBJECT

private slots:
    void parseRequestLine_valid() {
        const RemoteProtocol::RequestLine req =
            RemoteProtocol::parseRequestLine(QStringLiteral("GET /api/status HTTP/1.1\r\nHost: localhost\r\n\r\n"));
        QVERIFY(req.valid);
        QCOMPARE(req.method, QStringLiteral("GET"));
        QCOMPARE(req.path, QStringLiteral("/api/status"));
    }

    void parseRequestLine_malformed() {
        const RemoteProtocol::RequestLine req =
            RemoteProtocol::parseRequestLine(QStringLiteral("GET\r\n"));
        QVERIFY(!req.valid);
    }

    void parseRequestLine_empty() {
        const RemoteProtocol::RequestLine req = RemoteProtocol::parseRequestLine(QString());
        QVERIFY(!req.valid);
    }

    void matchRoute_allRoutes() {
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/")),
                 RemoteProtocol::Route::Root);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/cam?d=abc")),
                 RemoteProtocol::Route::Cam);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/status")),
                 RemoteProtocol::Route::Status);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/fader?val=50")),
                 RemoteProtocol::Route::Fader);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/cut")),
                 RemoteProtocol::Route::Cut);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/take")),
                 RemoteProtocol::Route::Take);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/stage?id=1")),
                 RemoteProtocol::Route::Stage);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/live/play")),
                 RemoteProtocol::Route::LivePlay);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/live/pause")),
                 RemoteProtocol::Route::LivePause);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/live/toggle")),
                 RemoteProtocol::Route::LiveToggle);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/live/seek?val=3")),
                 RemoteProtocol::Route::LiveSeek);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/staging/play")),
                 RemoteProtocol::Route::StagingPlay);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/staging/pause")),
                 RemoteProtocol::Route::StagingPause);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/staging/toggle")),
                 RemoteProtocol::Route::StagingToggle);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/staging/seek?val=3")),
                 RemoteProtocol::Route::StagingSeek);
        for (const char *old : {"/api/auto", "/api/selectA?id=1", "/api/selectB?id=2", "/api/togglePlayA",
                                "/api/togglePlayB"})
            QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QString::fromLatin1(old)),
                     RemoteProtocol::Route::NotFound);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/setTransitionMode?index=3")),
                 RemoteProtocol::Route::SetTransitionMode);
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/setDuration?val=2.5")),
                 RemoteProtocol::Route::SetDuration);
    }

    void matchRoute_notFound() {
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/unknown")),
                 RemoteProtocol::Route::NotFound);
    }

    void matchRoute_methodNotAllowed() {
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("POST"), QStringLiteral("/api/status")),
                 RemoteProtocol::Route::MethodNotAllowed);
    }

    void intParam_present() {
        int val = -1;
        QVERIFY(RemoteProtocol::intParam(QStringLiteral("/api/fader?val=50"), QStringLiteral("val"), val));
        QCOMPARE(val, 50);
    }

    void intParam_missing() {
        int val = -1;
        QVERIFY(!RemoteProtocol::intParam(QStringLiteral("/api/fader"), QStringLiteral("val"), val));
    }

    void intParam_badValue() {
        int val = -1;
        QVERIFY(!RemoteProtocol::intParam(QStringLiteral("/api/fader?val=abc"), QStringLiteral("val"), val));
    }

    void doubleParam_present() {
        double val = 0.0;
        QVERIFY(RemoteProtocol::doubleParam(QStringLiteral("/api/setDuration?val=2.5"),
                                          QStringLiteral("val"), val));
        QCOMPARE(val, 2.5);
    }

    void stringParam_present() {
        QString id;
        QVERIFY(RemoteProtocol::stringParam(QStringLiteral("/api/stage?id=abc-42"),
                                          QStringLiteral("id"), id));
        QCOMPARE(id, QStringLiteral("abc-42"));
        QVERIFY(!RemoteProtocol::stringParam(QStringLiteral("/api/stage"), QStringLiteral("id"), id));
    }

    void buildStatusJson_roundTrip() {
        RemoteProtocol::StatusData status;
        status.fader = 75;
        status.hasDeck = true;
        status.live = QStringLiteral("cue-a");
        status.staging = QStringLiteral("cue-b");
        status.isPlayingLive = true;
        status.isPlayingStaging = false;

        RemoteProtocol::CueInfo clip;
        clip.id = QStringLiteral("cue-a");
        clip.name = QStringLiteral("Intro");
        clip.onLive = true;
        clip.onStaging = false;
        status.cues.append(clip);

        status.hasTransition = true;
        status.transitionMode = 2;
        status.transitionDuration = 1.5;
        status.transitionModes = {QStringLiteral("Cut"), QStringLiteral("Fade")};

        const QJsonDocument doc = QJsonDocument::fromJson(RemoteProtocol::buildStatusJson(status));
        QVERIFY(doc.isObject());
        const QJsonObject obj = doc.object();

        QCOMPARE(obj.value(QStringLiteral("fader")).toInt(), 75);
        QCOMPARE(obj.value(QStringLiteral("live")).toString(), QStringLiteral("cue-a"));
        QCOMPARE(obj.value(QStringLiteral("staging")).toString(), QStringLiteral("cue-b"));
        QVERIFY(obj.value(QStringLiteral("isPlayingLive")).toBool());
        QVERIFY(!obj.value(QStringLiteral("isPlayingStaging")).toBool());
        QCOMPARE(obj.value(QStringLiteral("transitionMode")).toInt(), 2);
        QCOMPARE(obj.value(QStringLiteral("transitionDuration")).toDouble(), 1.5);

        const QJsonArray clips = obj.value(QStringLiteral("cues")).toArray();
        QCOMPARE(clips.size(), 1);
        const QJsonObject clipObj = clips.first().toObject();
        QCOMPARE(clipObj.value(QStringLiteral("id")).toString(), QStringLiteral("cue-a"));
        QCOMPARE(clipObj.value(QStringLiteral("name")).toString(), QStringLiteral("Intro"));
        QVERIFY(clipObj.value(QStringLiteral("onLive")).toBool());
        QVERIFY(!clipObj.value(QStringLiteral("onStaging")).toBool());

        const QJsonArray modes = obj.value(QStringLiteral("transitionModes")).toArray();
        QCOMPARE(modes.size(), 2);
        QCOMPARE(modes.at(0).toString(), QStringLiteral("Cut"));
    }

    void buildStatusJson_minimal() {
        RemoteProtocol::StatusData status;
        status.fader = 0;

        const QJsonDocument doc = QJsonDocument::fromJson(RemoteProtocol::buildStatusJson(status));
        const QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QStringLiteral("fader")).toInt(), 0);
        QVERIFY(!obj.contains(QStringLiteral("live")));
        QVERIFY(!obj.contains(QStringLiteral("transitionMode")));
    }

    void buildHttpResponse_ok() {
        const QByteArray body = "{\"status\":\"ok\"}";
        const QByteArray response = RemoteProtocol::buildHttpResponse(200, "application/json", body);

        QVERIFY(response.startsWith("HTTP/1.1 200 OK\r\n"));
        QVERIFY(response.contains("Content-Type: application/json\r\n"));
        QVERIFY(response.contains("Content-Length: " + QByteArray::number(body.size()) + "\r\n"));
        QVERIFY(response.endsWith("\r\n\r\n" + body));
    }

    void buildHttpResponse_error() {
        const QByteArray body = "404 Not Found";
        const QByteArray response = RemoteProtocol::buildHttpResponse(404, "text/plain", body);

        QVERIFY(response.startsWith("HTTP/1.1 404\r\n"));
        QVERIFY(response.contains("Content-Length: " + QByteArray::number(body.size()) + "\r\n"));
        QVERIFY(response.endsWith("\r\n\r\n" + body));
    }
};

QTEST_APPLESS_MAIN(TestRemoteControl)
#include "test_remotecontrol.moc"
