#include "mcp/McpCatalog.h"
#include "mcp/McpDispatcher.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpServer.h"
#include "mcp/McpSession.h"
#include "app/AppController.h"
#include "app/show/ShowManager.h"
#include "core/sources/SourceDescriptor.h"
#include "app/session/SessionManager.h"

#include <QtTest>
#include <QAbstractSocket>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

class TestMcp : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void catalogListsToolboxes();
    void catalogOpsIncludeWhen();
    void toolboxUnknownIsError();
    void toolboxReturnsSchemas();
    void toolboxAnnotationsPresent();
    void protocolInitializeAndToolsList();
    void protocolUnknownOp();
    void protocolNotificationHasNoReply();
    void sessionFileRoundTrip();
    void sessionFileMissing();
    void serverRequiresBearerToken();
    void serverInitializeWithToken();
    void applyUnknownOp();
    void addSourceAndInspect();
    void applyBatchStops();
    void setSpeedByClip();
    void renameNonInputNode();
    void addProcessNodeAndConnect();
    void stagedDeckIsPausedUntilTake();
    void cutIsImmediateAndTakeIgnoresEmptyStaging();
    void deckToolsUseLiveStagingNames();
    void cueToolsAddAndEnter();
    void cueToolsEditAndReorder();
};

void TestMcp::initTestCase()
{
    // Keep AppController's session files out of the user's real config dir.
    QStandardPaths::setTestModeEnabled(true);
}

void TestMcp::init()
{
    // A run that crashed or was killed leaves a stale session lock behind in the test config dir.
    QFile::remove(SessionManager::lockFilePath());
}

static QJsonObject rpc(const QString &method, const QJsonObject &params = {}, int id = 1)
{
    QJsonObject o{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
    };
    if (!params.isEmpty())
        o.insert(QStringLiteral("params"), params);
    return o;
}

static QByteArray httpPost(quint16 port, const QByteArray &auth, const QByteArray &body,
                           int *statusOut = nullptr)
{
    QTcpSocket socket;
    QEventLoop loop;
    QObject::connect(&socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    socket.connectToHost(QStringLiteral("127.0.0.1"), port);
    if (socket.state() != QAbstractSocket::ConnectedState)
        loop.exec();
    if (socket.state() != QAbstractSocket::ConnectedState)
        return {};

    QByteArray req = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n";
    if (!auth.isEmpty()) {
        req += "Authorization: ";
        req += auth;
        req += "\r\n";
    }
    req += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    req += body;
    socket.write(req);

    QByteArray response;
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&]() {
        response += socket.readAll();
        if (response.contains("\r\n\r\n")) {
            const int sep = response.indexOf("\r\n\r\n");
            const QByteArray header = response.left(sep);
            const int length = [&] {
                const QByteArray lower = header.toLower();
                const int at = lower.indexOf("content-length:");
                if (at < 0)
                    return 0;
                return header.mid(at + 15).trimmed().toInt();
            }();
            if (response.size() >= sep + 4 + length)
                loop.quit();
        }
    });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    response += socket.readAll();

    const int sep = response.indexOf("\r\n\r\n");
    if (sep < 0)
        return {};
    if (statusOut) {
        const QByteArray line = response.left(response.indexOf('\n'));
        const auto parts = line.split(' ');
        *statusOut = parts.size() >= 2 ? parts.at(1).toInt() : 0;
    }
    return response.mid(sep + 4);
}

void TestMcp::catalogListsToolboxes()
{
    const QJsonObject cat = prism::mcp::catalogPayload();
    QVERIFY(cat.value(QStringLiteral("ok")).toBool());
    const QJsonArray boxes = cat.value(QStringLiteral("toolboxes")).toArray();
    QCOMPARE(boxes.size(), 7);
    QStringList names;
    for (const QJsonValue &v : boxes)
        names.append(v.toObject().value(QStringLiteral("name")).toString());
    QVERIFY(names.contains(QStringLiteral("sources")));
    QVERIFY(names.contains(QStringLiteral("decks")));
    QVERIFY(names.contains(QStringLiteral("transition")));
    QVERIFY(names.contains(QStringLiteral("session")));
    QVERIFY(names.contains(QStringLiteral("graph")));
}

void TestMcp::catalogOpsIncludeWhen()
{
    const QJsonObject cat = prism::mcp::catalogPayload();
    QVERIFY(cat.contains(QStringLiteral("endpoints")));
    QVERIFY(cat.contains(QStringLiteral("units")));
    QVERIFY(cat.contains(QStringLiteral("workflow")));
    QVERIFY(cat.contains(QStringLiteral("guide")));
    for (const QJsonValue &v : cat.value(QStringLiteral("toolboxes")).toArray()) {
        const QJsonArray ops = v.toObject().value(QStringLiteral("ops")).toArray();
        QVERIFY(!ops.isEmpty());
        QVERIFY(ops.at(0).toObject().contains(QStringLiteral("name")));
        QVERIFY(ops.at(0).toObject().contains(QStringLiteral("when")));
    }
}

void TestMcp::toolboxUnknownIsError()
{
    const QJsonObject payload = prism::mcp::toolboxPayload(QStringLiteral("nope"));
    QCOMPARE(payload.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(payload.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_toolbox"));
}

void TestMcp::toolboxReturnsSchemas()
{
    const QJsonObject payload = prism::mcp::toolboxPayload(QStringLiteral("sources"));
    QVERIFY(payload.value(QStringLiteral("ok")).toBool());
    const QJsonArray tools = payload.value(QStringLiteral("tools")).toArray();
    QVERIFY(tools.size() >= 6);
    bool sawAdd = false;
    for (const QJsonValue &v : tools) {
        if (v.toObject().value(QStringLiteral("name")).toString() == QLatin1String("add_clip")) {
            sawAdd = true;
            QVERIFY(v.toObject().contains(QStringLiteral("inputSchema")));
        }
    }
    QVERIFY(sawAdd);
}

void TestMcp::toolboxAnnotationsPresent()
{
    const QJsonObject payload = prism::mcp::toolboxPayload(QStringLiteral("sources"));
    bool sawList = false;
    for (const QJsonValue &v : payload.value(QStringLiteral("tools")).toArray()) {
        const QJsonObject tool = v.toObject();
        if (tool.value(QStringLiteral("name")).toString() == QLatin1String("list_clips")) {
            sawList = true;
            const QJsonObject ann = tool.value(QStringLiteral("annotations")).toObject();
            QVERIFY(ann.value(QStringLiteral("readOnlyHint")).toBool());
            QVERIFY(ann.value(QStringLiteral("idempotentHint")).toBool());
        }
    }
    QVERIFY(sawList);
}

void TestMcp::protocolInitializeAndToolsList()
{
    const QJsonValue init = prism::mcp::handleJsonRpc(rpc(QStringLiteral("initialize")), {}, {});
    QVERIFY(init.isObject());
    QCOMPARE(init.toObject().value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("serverInfo")).toObject()
                 .value(QStringLiteral("name")).toString(),
             QStringLiteral("prism"));

    const QJsonValue listed = prism::mcp::handleJsonRpc(rpc(QStringLiteral("tools/list")), {}, {});
    const QJsonArray tools = listed.toObject().value(QStringLiteral("result")).toObject()
                                 .value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 5);
}

void TestMcp::protocolUnknownOp()
{
    bool called = false;
    const QJsonObject call = rpc(QStringLiteral("tools/call"),
                                 {{QStringLiteral("name"), QStringLiteral("nope")},
                                  {QStringLiteral("arguments"), QJsonObject{}}});
    const QJsonValue reply = prism::mcp::handleJsonRpc(
        call, {},
        [&](const QString &, const QJsonObject &) {
            called = true;
            return QJsonObject{};
        });
    QVERIFY(!called);
    const QJsonArray content = reply.toObject().value(QStringLiteral("result")).toObject()
                                   .value(QStringLiteral("content")).toArray();
    QVERIFY(!content.isEmpty());
    const QJsonObject payload = QJsonDocument::fromJson(
                                    content.at(0).toObject().value(QStringLiteral("text")).toString().toUtf8())
                                    .object();
    QCOMPARE(payload.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_op"));
}

void TestMcp::protocolNotificationHasNoReply()
{
    QJsonObject note{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                     {QStringLiteral("method"), QStringLiteral("notifications/initialized")}};
    const QJsonValue reply = prism::mcp::handleJsonRpc(note, {}, {});
    QVERIFY(reply.isUndefined());
}

void TestMcp::sessionFileRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("s.json"));
    qputenv("PRISM_MCP_SESSION_PATH", path.toUtf8());
    QVERIFY(prism::mcp::writeSessionFile(4731, QStringLiteral("abc123")));
    quint16 port = 0;
    QString token;
    QString error;
    QVERIFY(prism::mcp::readSessionFile(&port, &token, &error));
    QCOMPARE(port, quint16(4731));
    QCOMPARE(token, QStringLiteral("abc123"));
    prism::mcp::removeSessionFile();
    qunsetenv("PRISM_MCP_SESSION_PATH");
}

void TestMcp::sessionFileMissing()
{
    qputenv("PRISM_MCP_SESSION_PATH", "/tmp/prism-mcp-does-not-exist-test.json");
    QString error;
    QVERIFY(!prism::mcp::readSessionFile(nullptr, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("Agent access")));
    qunsetenv("PRISM_MCP_SESSION_PATH");
}

void TestMcp::serverRequiresBearerToken()
{
    QTemporaryDir dir;
    qputenv("PRISM_MCP_SESSION_PATH", dir.filePath(QStringLiteral("s.json")).toUtf8());
    AppController window(nullptr);
    window.setMcpEnabled(true);
    QVERIFY2(window.mcpEnabled(), qPrintable(window.mcpServer() ? window.mcpServer()->error() : QStringLiteral("no server")));
    int status = 0;
    httpPost(window.mcpServer()->port(), {},
             QJsonDocument(rpc(QStringLiteral("initialize"))).toJson(QJsonDocument::Compact), &status);
    QCOMPARE(status, 401);
    window.setMcpEnabled(false);
    qunsetenv("PRISM_MCP_SESSION_PATH");
}

void TestMcp::serverInitializeWithToken()
{
    QTemporaryDir dir;
    qputenv("PRISM_MCP_SESSION_PATH", dir.filePath(QStringLiteral("s.json")).toUtf8());
    AppController window(nullptr);
    window.setMcpEnabled(true);
    QVERIFY(window.mcpEnabled());
    const QByteArray auth = "Bearer " + window.mcpServer()->token().toUtf8();
    int status = 0;
    const QByteArray body = httpPost(
        window.mcpServer()->port(), auth,
        QJsonDocument(rpc(QStringLiteral("initialize"))).toJson(QJsonDocument::Compact), &status);
    QCOMPARE(status, 200);
    const QJsonObject reply = QJsonDocument::fromJson(body).object();
    QCOMPARE(reply.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("serverInfo")).toObject()
                 .value(QStringLiteral("name")).toString(),
             QStringLiteral("prism"));
    window.setMcpEnabled(false);
    qunsetenv("PRISM_MCP_SESSION_PATH");
}

void TestMcp::applyUnknownOp()
{
    AppController window(nullptr);
    prism::mcp::McpDispatcher dispatcher(&window);
    const QJsonObject result = dispatcher.applyOne(QStringLiteral("nope"), {});
    QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_op"));
}

void TestMcp::addSourceAndInspect()
{
    AppController window(nullptr);
    prism::mcp::McpDispatcher dispatcher(&window);
    const QJsonObject added = dispatcher.applyOne(
        QStringLiteral("add_source"),
        {{QStringLiteral("kind"), QStringLiteral("text")},
         {QStringLiteral("text"), QStringLiteral("Hello Prism")},
         {QStringLiteral("name"), QStringLiteral("Lower Third")}});
    QVERIFY2(added.value(QStringLiteral("ok")).toBool(),
             qPrintable(added.value(QStringLiteral("detail")).toString()));
    const QString id = added.value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());

    const QJsonObject inspected = dispatcher.inspect(
        {{QStringLiteral("clips"), true}, {QStringLiteral("detail"), true}});
    QVERIFY(inspected.value(QStringLiteral("ok")).toBool());
    const QJsonArray clips = inspected.value(QStringLiteral("clips")).toArray();
    QCOMPARE(clips.size(), 1);
    QCOMPARE(clips.at(0).toObject().value(QStringLiteral("id")).toString(), id);
    QCOMPARE(clips.at(0).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("text"));
    QCOMPARE(clips.at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("Hello Prism"));

    const QJsonObject renamed = dispatcher.applyOne(
        QStringLiteral("rename_clip"),
        {{QStringLiteral("clip"), id}, {QStringLiteral("name"), QStringLiteral("Title")}});
    QVERIFY(renamed.value(QStringLiteral("ok")).toBool());
    QCOMPARE(renamed.value(QStringLiteral("name")).toString(), QStringLiteral("Title"));
}

void TestMcp::setSpeedByClip()
{
    AppController window(nullptr);
    prism::mcp::McpDispatcher dispatcher(&window);
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::VideoFile;
    desc.path = QStringLiteral("/nonexistent.mp4");
    const quint64 video = window.clipGraph()->addNode(ClipGraph::NodeType::Input, QPointF(0, 0),
                                                      {{"source", ClipGraph::sourceToMap(desc)}});
    const QJsonObject set = dispatcher.applyOne(
        QStringLiteral("set_speed"),
        {{QStringLiteral("clip"), QString::number(video)}, {QStringLiteral("speed"), 8.0}});
    QVERIFY(set.value(QStringLiteral("ok")).toBool());
    QCOMPARE(set.value(QStringLiteral("speed")).toDouble(), 4.0);
    QCOMPARE(window.clipGraph()->clipSettings(video).speed, 4.0);

    desc.kind = SourceDescriptor::Kind::Canvas;
    const quint64 canvas = window.clipGraph()->addNode(ClipGraph::NodeType::Input, QPointF(0, 0),
                                                       {{"source", ClipGraph::sourceToMap(desc)}});
    const QJsonObject bad = dispatcher.applyOne(
        QStringLiteral("set_speed"),
        {{QStringLiteral("clip"), QString::number(canvas)}, {QStringLiteral("speed"), 2.0}});
    QCOMPARE(bad.value(QStringLiteral("error")).toString(), QStringLiteral("type_mismatch"));
}

void TestMcp::renameNonInputNode()
{
    AppController window(nullptr);
    ClipGraph *graph = window.clipGraph();
    const quint64 layer = graph->addNode(ClipGraph::NodeType::Layer, QPointF(0, 0), {});
    const QString original = graph->node(layer)->title;
    window.renameNode(layer, QStringLiteral("Main Layer"));
    QCOMPARE(graph->node(layer)->title, QStringLiteral("Main Layer"));
    ClipGraph copy;
    QVERIFY(copy.fromJson(graph->toJson()));
    QCOMPARE(copy.node(layer)->title, QStringLiteral("Main Layer"));
    window.graphController()->undo();
    QCOMPARE(graph->node(layer)->title, original);
}

void TestMcp::applyBatchStops()
{
    AppController window(nullptr);
    prism::mcp::McpDispatcher dispatcher(&window);
    const QJsonObject result = dispatcher.apply(QJsonObject{
        {QStringLiteral("ops"),
         QJsonArray{
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("set_transition_progress")},
                         {QStringLiteral("args"), QJsonObject{{QStringLiteral("value"), 25}}}},
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("nope")}},
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("set_transition_progress")},
                         {QStringLiteral("args"), QJsonObject{{QStringLiteral("value"), 80}}}},
         }}});
    QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("apply_failed"));
    QCOMPARE(result.value(QStringLiteral("stopped")).toInt(), 1);
    QCOMPARE(window.faderValue(), 25);
}

void TestMcp::addProcessNodeAndConnect()
{
    AppController window(nullptr);
    prism::mcp::McpDispatcher dispatcher(&window);

    const QJsonObject graphBox = prism::mcp::toolboxPayload(QStringLiteral("graph"));
    QVERIFY(graphBox.value(QStringLiteral("ok")).toBool());
    QStringList opNames;
    for (const QJsonValue &v : graphBox.value(QStringLiteral("tools")).toArray())
        opNames.append(v.toObject().value(QStringLiteral("name")).toString());
    QVERIFY(opNames.contains(QStringLiteral("add_process_node")));
    QVERIFY(opNames.contains(QStringLiteral("connect")));

    const QJsonObject effects = dispatcher.applyOne(QStringLiteral("list_process_effects"), {});
    QVERIFY(effects.value(QStringLiteral("ok")).toBool());
    QVERIFY(effects.value(QStringLiteral("effects")).toArray().size() >= 4);

    const QJsonObject input = dispatcher.applyOne(
        QStringLiteral("add_source"),
        {{QStringLiteral("kind"), QStringLiteral("canvas")},
         {QStringLiteral("name"), QStringLiteral("MCP Canvas")},
         {QStringLiteral("w"), 1280},
         {QStringLiteral("h"), 720}});
    QVERIFY2(input.value(QStringLiteral("ok")).toBool(),
             qPrintable(input.value(QStringLiteral("detail")).toString()));
    const QString inputId = input.value(QStringLiteral("id")).toString();
    QVERIFY(!inputId.isEmpty());

    const QJsonObject crop = dispatcher.applyOne(
        QStringLiteral("add_process_node"),
        {{QStringLiteral("effect"), QStringLiteral("crop")},
         {QStringLiteral("params"), QJsonObject{{QStringLiteral("x"), 0.1},
                                                {QStringLiteral("w"), 0.8}}}});
    QVERIFY2(crop.value(QStringLiteral("ok")).toBool(),
             qPrintable(crop.value(QStringLiteral("detail")).toString()));
    const QString cropId = crop.value(QStringLiteral("id")).toString();
    QVERIFY(!cropId.isEmpty());
    QCOMPARE(crop.value(QStringLiteral("name")).toString(), QStringLiteral("Crop"));

    const QJsonObject listed = dispatcher.applyOne(QStringLiteral("list_nodes"), {});
    QVERIFY(listed.value(QStringLiteral("ok")).toBool());
    const QString outputId = listed.value(QStringLiteral("output")).toString();
    QVERIFY(!outputId.isEmpty());

    const QJsonObject wireIn = dispatcher.applyOne(
        QStringLiteral("connect"),
        {{QStringLiteral("from"), inputId}, {QStringLiteral("to"), cropId}});
    QVERIFY2(wireIn.value(QStringLiteral("ok")).toBool(),
             qPrintable(wireIn.value(QStringLiteral("detail")).toString()));
    QCOMPARE(wireIn.value(QStringLiteral("kindName")).toString(), QStringLiteral("chain"));

    const QJsonObject wireOut = dispatcher.applyOne(
        QStringLiteral("connect"),
        {{QStringLiteral("from"), cropId}, {QStringLiteral("to"), outputId}});
    QVERIFY(wireOut.value(QStringLiteral("ok")).toBool());

    const QJsonObject inspected = dispatcher.inspect({{QStringLiteral("graph"), true}});
    QVERIFY(inspected.value(QStringLiteral("ok")).toBool());
    const QJsonObject graph = inspected.value(QStringLiteral("graph")).toObject();
    QCOMPARE(graph.value(QStringLiteral("connections")).toArray().size(), 2);

    const QJsonObject params = dispatcher.applyOne(
        QStringLiteral("set_process_params"),
        {{QStringLiteral("node"), cropId},
         {QStringLiteral("params"), QJsonObject{{QStringLiteral("y"), 0.2}}}});
    QVERIFY(params.value(QStringLiteral("ok")).toBool());

    QVERIFY(dispatcher
                .applyOne(QStringLiteral("disconnect"),
                          {{QStringLiteral("from"), cropId}, {QStringLiteral("to"), outputId}})
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(dispatcher.applyOne(QStringLiteral("remove_node"), {{QStringLiteral("node"), cropId}})
                .value(QStringLiteral("ok"))
                .toBool());
}

static quint64 addStillClip(AppController &window, const QString &path, const QColor &color)
{
    QImage img(32, 32, QImage::Format_RGB888);
    img.fill(color);
    if (!img.save(path))
        return 0;
    return window.addSourceFromMap({{QStringLiteral("kind"), int(SourceDescriptor::Kind::VideoFile)},
                                    {QStringLiteral("path"), path}});
}

// A new cue holding one still clip wired to its Output node; leaves it as the edited cue.
static QString addClipCue(AppController &window, const QString &path, const QColor &color, quint64 *clipOut)
{
    const QString cue = window.addCue();
    window.enterCue(cue);
    *clipOut = addStillClip(window, path, color);
    if (!*clipOut || !window.clipGraph()->connectNodes(*clipOut, window.clipGraph()->outputNodeId()))
        return {};
    return cue;
}

void TestMcp::stagedDeckIsPausedUntilTake()
{
    QTemporaryDir dir;
    AppController window(nullptr);
    quint64 clip = 0;
    const QString cue = addClipCue(window, dir.filePath(QStringLiteral("a.png")), Qt::red, &clip);
    QVERIFY(!cue.isEmpty());
    QCOMPARE(window.activeNodeLive(), quint64(0));

    QVERIFY(window.stageCue(cue));
    QTRY_VERIFY(window.compositor()->sourceStaging());
    QCOMPARE(window.activeNodeStaging(), clip);
    QVERIFY(!window.isPlayingStaging());

    window.setTransitionDuration(0.05);
    window.take();
    QTRY_COMPARE(window.activeNodeLive(), clip);
    QTRY_VERIFY(window.isPlayingLive());
    QCOMPARE(window.faderValue(), 0);
    QVERIFY(!window.transitionRunning());
    QCOMPARE(window.liveCueId(), cue);
    QVERIFY(!window.stagingCueId().isEmpty());
    QVERIFY(window.stagingCueId() != cue);
}

void TestMcp::cutIsImmediateAndTakeIgnoresEmptyStaging()
{
    QTemporaryDir dir;
    AppController window(nullptr);
    window.take();
    window.cut();
    QCOMPARE(window.activeNodeLive(), quint64(0));
    QVERIFY(!window.transitionRunning());

    quint64 clip = 0;
    const QString cue = addClipCue(window, dir.filePath(QStringLiteral("b.png")), Qt::blue, &clip);
    QVERIFY(!cue.isEmpty());
    QVERIFY(window.stageCue(cue));
    window.cut();
    QCOMPARE(window.activeNodeLive(), clip);
    QCOMPARE(window.faderValue(), 0);
    QVERIFY(window.isPlayingLive());
    QVERIFY(window.compositor()->crossfade() < 0.01f);
}

void TestMcp::deckToolsUseLiveStagingNames()
{
    QTemporaryDir dir;
    AppController window(nullptr);
    prism::mcp::McpDispatcher dispatcher(&window);
    quint64 clip = 0;
    const QString cue = addClipCue(window, dir.filePath(QStringLiteral("c.png")), Qt::green, &clip);
    QVERIFY(!cue.isEmpty());
    const QString id = QString::number(clip);

    const QJsonObject cues = dispatcher.applyOne(QStringLiteral("list_cues"), {});
    QCOMPARE(cues.value(QStringLiteral("cues")).toArray().size(), 2);
    QCOMPARE(dispatcher.applyOne(QStringLiteral("stage"), {{QStringLiteral("clip"), id}})
                 .value(QStringLiteral("error")).toString(),
             QStringLiteral("unknown_op"));
    QCOMPARE(dispatcher.applyOne(QStringLiteral("stage_cue"), {{QStringLiteral("cue"), QStringLiteral("nope")}})
                 .value(QStringLiteral("error")).toString(),
             QStringLiteral("not_found"));
    const QJsonObject staged = dispatcher.applyOne(QStringLiteral("stage_cue"), {{QStringLiteral("cue"), 2}});
    QVERIFY(staged.value(QStringLiteral("ok")).toBool());
    QCOMPARE(staged.value(QStringLiteral("id")).toString(), cue);
    QTRY_COMPARE(window.activeNodeStaging(), clip);

    QVERIFY(dispatcher.applyOne(QStringLiteral("play"), {{QStringLiteral("deck"), QStringLiteral("staging")}})
                .value(QStringLiteral("ok")).toBool());
    QVERIFY(dispatcher.applyOne(QStringLiteral("pause"), {{QStringLiteral("deck"), QStringLiteral("live")}})
                .value(QStringLiteral("ok")).toBool());
    QVERIFY(dispatcher.applyOne(QStringLiteral("seek"),
                                {{QStringLiteral("deck"), QStringLiteral("staging")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(dispatcher.applyOne(QStringLiteral("play"), {{QStringLiteral("deck"), QStringLiteral("a")}})
                 .value(QStringLiteral("error")).toString(),
             QStringLiteral("bad_args"));
    QCOMPARE(dispatcher.applyOne(QStringLiteral("play"), {}).value(QStringLiteral("error")).toString(),
             QStringLiteral("bad_args"));
    QCOMPARE(dispatcher.applyOne(QStringLiteral("select_a"), {{QStringLiteral("clip"), id}})
                 .value(QStringLiteral("error")).toString(),
             QStringLiteral("unknown_op"));

    window.setTransitionDuration(0.05);
    QVERIFY(dispatcher.applyOne(QStringLiteral("take"), {}).value(QStringLiteral("ok")).toBool());
    QTRY_COMPARE(window.activeNodeLive(), clip);
    QCOMPARE(window.faderValue(), 0);
}

void TestMcp::cueToolsAddAndEnter()
{
    AppController window(nullptr);
    prism::mcp::McpDispatcher dispatcher(&window);
    const QJsonObject added = dispatcher.applyOne(QStringLiteral("add_cue"), {{QStringLiteral("name"), QStringLiteral("Intro")}});
    QVERIFY(added.value(QStringLiteral("ok")).toBool());
    QCOMPARE(added.value(QStringLiteral("name")).toString(), QStringLiteral("Intro"));
    const QString id = added.value(QStringLiteral("id")).toString();
    QVERIFY(dispatcher.applyOne(QStringLiteral("enter_cue"), {{QStringLiteral("cue"), id}})
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(window.editedCueId(), id);
    QCOMPARE(dispatcher.applyOne(QStringLiteral("add_ab_select"), {}).value(QStringLiteral("error")).toString(),
             QStringLiteral("unknown_op"));
}

void TestMcp::cueToolsEditAndReorder()
{
    AppController window(nullptr);
    prism::mcp::McpDispatcher dispatcher(&window);
    auto call = [&](const char *tool, const QJsonObject &args) {
        return dispatcher.applyOne(QString::fromLatin1(tool), args);
    };
    const QString first = window.liveCueId();
    const QString a = call("add_cue", {{"name", "A"}}).value(QStringLiteral("id")).toString();
    const QString b = call("add_cue", {{"name", "B"}}).value(QStringLiteral("id")).toString();

    QVERIFY(window.showManager()->indexOf(b) > 0);
    QVERIFY(call("reorder_cue", {{"cue", b}, {"to", 1}}).value(QStringLiteral("ok")).toBool());
    QCOMPARE(window.showManager()->indexOf(b), 0);
    QCOMPARE(call("reorder_cue", {{"cue", b}, {"to", 9}}).value(QStringLiteral("error")).toString(),
             QStringLiteral("bad_args"));

    const QJsonObject props = call("set_cue_props", {{"cue", a}, {"color", "#ff0000"}, {"notes", "n"},
                                                      {"endAction", "loop"}, {"hotkey", "Q"},
                                                      {"transition", QJsonObject{{"mode", 1}, {"durationMs", 250}}}});
    QVERIFY(props.value(QStringLiteral("ok")).toBool());
    QCOMPARE(props.value(QStringLiteral("endAction")).toString(), QStringLiteral("loop"));
    QCOMPARE(props.value(QStringLiteral("hotkey")).toString(), QStringLiteral("Q"));
    QCOMPARE(props.value(QStringLiteral("transition")).toObject().value(QStringLiteral("durationMs")).toInt(), 250);
    QVERIFY(call("set_cue_props", {{"cue", a}, {"transition", QJsonValue::Null}})
                .value(QStringLiteral("transition")).isNull());
    QCOMPARE(call("set_cue_props", {{"cue", a}, {"transition", QJsonObject{{"mode", "nope"}}}})
                 .value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));

    QVERIFY(call("set_default_transition", {{"mode", 0}, {"durationMs", 700}}).value(QStringLiteral("ok")).toBool());
    QCOMPARE(window.showManager()->defaultTransition(), (TransitionSpec{0, 700}));

    const QJsonObject dup = call("duplicate_cue", {{"cue", a}});
    QVERIFY(dup.value(QStringLiteral("ok")).toBool());
    const QString dupId = dup.value(QStringLiteral("id")).toString();
    QVERIFY(dupId != a);
    QVERIFY(call("remove_cue", {{"cue", dupId}}).value(QStringLiteral("ok")).toBool());
    QVERIFY(!window.showManager()->cue(dupId));
    QCOMPARE(call("remove_cue", {{"cue", first}}).value(QStringLiteral("error")).toString(), QStringLiteral("conflict"));

    QVERIFY(call("take_cue", {{"cue", a}}).value(QStringLiteral("ok")).toBool());
    QVERIFY(call("exit_cue", {}).value(QStringLiteral("ok")).toBool());
    QVERIFY(!window.inCue());

    QJsonObject pre = call("preflight", {});
    QVERIFY(pre.value(QStringLiteral("ok")).toBool());
    QVERIFY(pre.value(QStringLiteral("issues")).isArray());
}

QTEST_MAIN(TestMcp)
#include "test_mcp.moc"
