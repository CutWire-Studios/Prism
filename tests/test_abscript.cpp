#include "core/graph/ClipGraph.h"
#include "app/graph/GraphRuntime.h"
#include "core/sources/SourceDescriptor.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>

// End-to-end coverage for scripted A/B deck switching: a Script node wired into
// an A/B Select node's data-in port should drive assignInputToDeck the same way
// a manual A/B button click does.
class TestAbScript : public QObject {
    Q_OBJECT

    ClipGraph *m_graph = nullptr;
    GraphRuntime *m_runtime = nullptr;

private slots:
    void init() {
        m_graph = new ClipGraph;
        m_runtime = new GraphRuntime(m_graph);
    }

    void cleanup() {
        delete m_runtime;
        delete m_graph;
        m_runtime = nullptr;
        m_graph = nullptr;
    }

    // Clip(1) --slot0--\                                   Script(4) --dataIn--\
    //                    A/B Select(3) --AbToOutput--> Output(5)                (2 is Clip B)
    // Clip(2) --slot1--/
    QJsonObject makeGraph(const QString &luaCode) const {
        auto clip = [](qint64 id) {
            QJsonObject src;
            src["kind"] = (int)SourceDescriptor::Kind::Canvas;
            src["displayName"] = QStringLiteral("Clip%1").arg(id);
            QJsonObject o;
            o["id"] = id;
            o["source"] = src;
            o["hasAudio"] = false;
            return o;
        };

        QJsonObject ab;
        ab["id"] = 3;
        QJsonObject slotA; slotA["name"] = "Camera A";
        QJsonObject slotB; slotB["name"] = "Camera B";
        ab["slots"] = QJsonArray{slotA, slotB};

        QJsonObject output;
        output["id"] = 5;

        QJsonObject script;
        script["id"] = 4;
        script["luaCode"] = luaCode;
        script["triggerMode"] = 1; // ScriptTriggerMode::OnStart
        script["intervalMs"] = 1000;

        auto conn = [](qint64 from, qint64 to, int kind, int toPortIndex) {
            QJsonObject c;
            c["from"] = from; c["to"] = to;
            c["kind"] = kind; c["toPortIndex"] = toPortIndex;
            return c;
        };
        // Chain=0, ScriptToData=6, AbToOutput=8 (ClipGraph::EdgeKind).
        QJsonArray connections{
            conn(1, 3, 0, 0),
            conn(2, 3, 0, 1),
            conn(3, 5, 8, -1),
            conn(4, 3, 6, -1),
        };

        QJsonObject state;
        state["graphVersion"] = 3;
        state["inputNodes"] = QJsonArray{clip(1), clip(2)};
        state["abSelectNodes"] = QJsonArray{ab};
        state["outputNode"] = output;
        state["scriptNodes"] = QJsonArray{script};
        state["connections"] = connections;
        return state;
    }

    void scriptDrivesDeckAssignment() {
        QVERIFY(m_graph->fromJson(makeGraph(
            "return { a = \"Camera A\", b = \"Camera B\" }")));

        // Script runs OnStart on its own thread, then the editor's poll timer
        // (100ms) applies the result; give both a generous window.
        QTRY_COMPARE_WITH_TIMEOUT(m_graph->deckAInput(), NodeId(1), 3000);
        QCOMPARE(m_graph->deckBInput(), NodeId(2));
    }

    void scriptResolvesSlotsByOneBasedIndex() {
        QVERIFY(m_graph->fromJson(makeGraph("return { a = 2, b = 1 }")));

        QTRY_COMPARE_WITH_TIMEOUT(m_graph->deckAInput(), NodeId(2), 3000);
        QCOMPARE(m_graph->deckBInput(), NodeId(1));
    }
};

QTEST_MAIN(TestAbScript)
#include "test_abscript.moc"
