#include "app/hotkeys/HotkeyManager.h"
#include "core/graph/ClipGraph.h"
#include "core/sources/SourceDescriptor.h"

#include <QtTest>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>

class TestHotkeys : public QObject {
    Q_OBJECT

    ClipGraph *m_graph = nullptr;

private slots:
    void init() {
        m_graph = new ClipGraph;
    }

    void cleanup() {
        delete m_graph;
        m_graph = nullptr;
    }

    // Two canvas inputs (ids 1, 2) wired into a 3-slot Switcher (id 3),
    // whose output feeds the Output node (id 4).
    QJsonObject makeGraph() const {
        auto canvasSource = [](const QString &name) {
            QJsonObject src;
            src["kind"] = (int)SourceDescriptor::Kind::Canvas;
            src["displayName"] = name;
            return src;
        };
        auto inputNode = [&](qint64 id, const QString &name) {
            QJsonObject n;
            n["id"] = id;
            n["source"] = canvasSource(name);
            n["hasAudio"] = false;
            return n;
        };

        QJsonArray inputNodes{inputNode(1, "Clip A"), inputNode(2, "Clip B")};

        QJsonObject ab;
        ab["id"] = 3;
        QJsonArray slotArr{QJsonObject{{"name", ""}}, QJsonObject{{"name", ""}},
                           QJsonObject{{"name", ""}}};
        ab["slots"] = slotArr;

        QJsonObject output;
        output["id"] = 4;

        auto conn = [](qint64 from, qint64 to, int kind, int toPortIndex = -1) {
            QJsonObject c;
            c["from"] = from;
            c["to"] = to;
            c["kind"] = kind;
            c["toPortIndex"] = toPortIndex;
            return c;
        };
        QJsonArray connections{
            conn(1, 3, 0 /*Chain*/, 0),
            conn(2, 3, 0 /*Chain*/, 1),
            conn(3, 4, 8 /*AbToOutput*/),
        };

        QJsonObject state;
        state["graphVersion"] = ClipGraph::kGraphVersion;
        state["nextId"] = 5;
        state["inputNodes"] = inputNodes;
        state["abSelectNodes"] = QJsonArray{ab};
        state["outputNode"] = output;
        state["connections"] = connections;
        return state;
    }

    void syncAssignsKeysToSwitcherInputs() {
        QVERIFY(m_graph->fromJson(makeGraph()));
        const QVector<ClipGraph::AbSlotInfo> inputs = m_graph->abSelectInputs();
        QCOMPARE(inputs.size(), 2);

        HotkeyManager mgr(m_graph);
        mgr.syncWithGraph();

        const Qt::Key k1 = mgr.bindingForSlot(inputs[0].ref);
        const Qt::Key k2 = mgr.bindingForSlot(inputs[1].ref);
        QVERIFY(HotkeyManager::isBindableKey(k1));
        QVERIFY(HotkeyManager::isBindableKey(k2));
        QVERIFY(k1 != k2);
        QVERIFY(mgr.slotForKey(k1) == inputs[0].ref);

        QCOMPARE(m_graph->abSlotHotkeyLabel(inputs[0].ref), QKeySequence(k1).toString());
        QCOMPARE(m_graph->abSlotHotkeyLabel(inputs[1].ref), QKeySequence(k2).toString());

        // A key taken by another input is rejected.
        QVERIFY(!mgr.setBinding(inputs[0].ref, k2));

        mgr.clearBinding(inputs[0].ref);
        QCOMPARE(mgr.bindingForSlot(inputs[0].ref), Qt::Key_unknown);
        QVERIFY(!mgr.slotForKey(k1).isValid());
        QVERIFY(mgr.slotForKey(k2) == inputs[1].ref);
    }

    void triggerAbSlotAssignsDecks() {
        QVERIFY(m_graph->fromJson(makeGraph()));
        const QVector<ClipGraph::AbSlotInfo> inputs = m_graph->abSelectInputs();
        QCOMPARE(inputs.size(), 2);

        QVERIFY(m_graph->triggerAbSlot(inputs[0].ref, Deck::Live));
        QCOMPARE(m_graph->liveInput(), inputs[0].producer);
        QVERIFY(m_graph->triggerAbSlot(inputs[1].ref, Deck::Staging));
        QCOMPARE(m_graph->stagingInput(), inputs[1].producer);

        QVERIFY(!m_graph->triggerAbSlot(ClipGraph::AbSlotRef{999, 0}, Deck::Live));
    }

    void restoreMapsLegacyClipBindings() {
        QVERIFY(m_graph->fromJson(makeGraph()));
        const QVector<ClipGraph::AbSlotInfo> inputs = m_graph->abSelectInputs();

        HotkeyManager mgr(m_graph);
        QJsonArray legacy;
        legacy.append(QJsonObject{{"nodeId", 1}, {"key", (int)Qt::Key_G}});
        mgr.restoreHotkeys(legacy);

        // Clip 1 feeds slot 0 → the binding lands on that switcher input.
        QCOMPARE(mgr.bindingForSlot(inputs[0].ref), Qt::Key_G);
        // The remaining input was auto-assigned.
        QVERIFY(HotkeyManager::isBindableKey(mgr.bindingForSlot(inputs[1].ref)));
    }

    void isBindableKey_rejectsModifiers() {
        QVERIFY(!HotkeyManager::isBindableKey(Qt::Key_Shift));
        QVERIFY(!HotkeyManager::isBindableKey(Qt::Key_Control));
        QVERIFY(HotkeyManager::isBindableKey(Qt::Key_1));
    }

    void exportImportProfile() {
        QVERIFY(m_graph->fromJson(makeGraph()));
        const QVector<ClipGraph::AbSlotInfo> inputs = m_graph->abSelectInputs();

        HotkeyManager mgr(m_graph);
        mgr.syncWithGraph();
        const Qt::Key key = mgr.bindingForSlot(inputs[0].ref);
        QVERIFY(HotkeyManager::isBindableKey(key));

        QSignalSpy changed(&mgr, &HotkeyManager::bindingsChanged);
        const QJsonObject profile = mgr.exportProfile();
        mgr.clearBinding(inputs[0].ref);
        QVERIFY(mgr.importProfile(profile));
        QCOMPARE(mgr.bindingForSlot(inputs[0].ref), key);
        QVERIFY(changed.count() >= 1);
    }

    void mainWindowOrderShowsLabelsOnGraphRestore() {
        HotkeyManager mgr(m_graph);
        connect(m_graph, &ClipGraph::changed, &mgr, &HotkeyManager::syncWithGraph);
        QVERIFY(m_graph->fromJson(makeGraph()));

        const QVector<ClipGraph::AbSlotInfo> inputs = m_graph->abSelectInputs();
        QCOMPARE(inputs.size(), 2);
        for (const ClipGraph::AbSlotInfo &info : inputs) {
            const Qt::Key key = mgr.bindingForSlot(info.ref);
            QVERIFY(HotkeyManager::isBindableKey(key));
            QCOMPARE(m_graph->abSlotHotkeyLabel(info.ref), QKeySequence(key).toString());
        }
    }

    void sessionJsonRoundTrip() {
        QVERIFY(m_graph->fromJson(makeGraph()));
        const QVector<ClipGraph::AbSlotInfo> inputs = m_graph->abSelectInputs();

        HotkeyManager mgr(m_graph);
        QVERIFY(mgr.setBinding(inputs[0].ref, Qt::Key_5));
        QVERIFY(mgr.setBinding(inputs[1].ref, Qt::Key_K));

        const QJsonArray saved = mgr.hotkeysJson();
        mgr.clearBinding(inputs[0].ref);
        mgr.clearBinding(inputs[1].ref);
        mgr.restoreHotkeys(saved);

        QCOMPARE(mgr.bindingForSlot(inputs[0].ref), Qt::Key_5);
        QCOMPARE(mgr.bindingForSlot(inputs[1].ref), Qt::Key_K);
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    TestHotkeys tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_hotkeys.moc"
