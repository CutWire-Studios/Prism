#include "mcp/McpCatalog.h"
#include "mcp/McpDispatcher.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpServer.h"
#include "mcp/McpSession.h"
#include "ui/mainwindow/MainWindow.h"

#include <QtTest>
#include <QAbstractSocket>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

class TestMcp : public QObject {
    Q_OBJECT

private slots:
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
};

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
    QCOMPARE(boxes.size(), 6);
    QStringList names;
    for (const QJsonValue &v : boxes)
        names.append(v.toObject().value(QStringLiteral("name")).toString());
    QVERIFY(names.contains(QStringLiteral("sources")));
    QVERIFY(names.contains(QStringLiteral("decks")));
    QVERIFY(names.contains(QStringLiteral("transition")));
    QVERIFY(names.contains(QStringLiteral("session")));
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
    MainWindow window;
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
    MainWindow window;
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
    MainWindow window;
    prism::mcp::McpDispatcher dispatcher(&window);
    const QJsonObject result = dispatcher.applyOne(QStringLiteral("nope"), {});
    QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_op"));
}

void TestMcp::addSourceAndInspect()
{
    MainWindow window;
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

void TestMcp::applyBatchStops()
{
    MainWindow window;
    prism::mcp::McpDispatcher dispatcher(&window);
    const QJsonObject result = dispatcher.apply(QJsonObject{
        {QStringLiteral("ops"),
         QJsonArray{
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("set_fader")},
                         {QStringLiteral("args"), QJsonObject{{QStringLiteral("value"), 25}}}},
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("nope")}},
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("set_fader")},
                         {QStringLiteral("args"), QJsonObject{{QStringLiteral("value"), 80}}}},
         }}});
    QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("apply_failed"));
    QCOMPARE(result.value(QStringLiteral("stopped")).toInt(), 1);
    QCOMPARE(window.faderValue(), 25);
}

QTEST_MAIN(TestMcp)
#include "test_mcp.moc"
