#include "ui/hotkeys/HotkeyManager.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "core/sources/SourceDescriptor.h"

#include <QtTest>
#include <QApplication>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>

class TestHotkeys : public QObject {
    Q_OBJECT

    QWidget *m_parent = nullptr;
    ClipNodeEditor *m_editor = nullptr;

private slots:
    void init() {
        m_parent = new QWidget;
        m_editor = new ClipNodeEditor(m_parent);
    }

    void cleanup() {
        delete m_parent;
        m_parent = nullptr;
        m_editor = nullptr;
    }

    // Two canvas inputs (ids 1, 2) wired into a 3-slot A/B Select (id 3),
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
        state["graphVersion"] = 3;
        state["nextId"] = 5;
        state["inputNodes"] = inputNodes;
        state["abSelectNodes"] = QJsonArray{ab};
        state["outputNode"] = output;
        state["connections"] = connections;
        return state;
    }

    void syncAssignsKeysToSwitcherInputs() {
        m_editor->restoreState(makeGraph());
        const QVector<AbSlotInfo> inputs = m_editor->abSelectInputs();
        QCOMPARE(inputs.size(), 2);

        HotkeyManager mgr(m_parent, m_editor);
        mgr.syncWithGraph();

        const Qt::Key k1 = mgr.bindingForSlot(inputs[0].ref);
        const Qt::Key k2 = mgr.bindingForSlot(inputs[1].ref);
        QVERIFY(HotkeyManager::isBindableKey(k1));
        QVERIFY(HotkeyManager::isBindableKey(k2));
        QVERIFY(k1 != k2);
        QVERIFY(mgr.slotForKey(k1) == inputs[0].ref);

        QCOMPARE(m_editor->abSlotHotkeyLabel(inputs[0].ref), QKeySequence(k1).toString());
        QCOMPARE(m_editor->abSlotHotkeyLabel(inputs[1].ref), QKeySequence(k2).toString());

        // A key taken by another input is rejected.
        QVERIFY(!mgr.setBinding(inputs[0].ref, k2));

        mgr.clearBinding(inputs[0].ref);
        QCOMPARE(mgr.bindingForSlot(inputs[0].ref), Qt::Key_unknown);
        QVERIFY(!mgr.slotForKey(k1).isValid());
        QVERIFY(mgr.slotForKey(k2) == inputs[1].ref);
    }

    void triggerAbSlotAssignsDecks() {
        m_editor->restoreState(makeGraph());
        const QVector<AbSlotInfo> inputs = m_editor->abSelectInputs();
        QCOMPARE(inputs.size(), 2);

        QVERIFY(m_editor->triggerAbSlot(inputs[0].ref, true));
        QCOMPARE(m_editor->deckAInput(), inputs[0].producer);
        QVERIFY(m_editor->triggerAbSlot(inputs[1].ref, false));
        QCOMPARE(m_editor->deckBInput(), inputs[1].producer);

        QVERIFY(!m_editor->triggerAbSlot(AbSlotRef{999, 0}, true));
    }

    void restoreMapsLegacyClipBindings() {
        m_editor->restoreState(makeGraph());
        const QVector<AbSlotInfo> inputs = m_editor->abSelectInputs();

        HotkeyManager mgr(m_parent, m_editor);
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
        m_editor->restoreState(makeGraph());
        const QVector<AbSlotInfo> inputs = m_editor->abSelectInputs();

        HotkeyManager mgr(m_parent, m_editor);
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
        HotkeyManager mgr(m_parent, m_editor);
        connect(m_editor, &ClipNodeEditor::clipChainChanged, &mgr, &HotkeyManager::syncWithGraph);
        m_editor->restoreState(makeGraph());

        const QVector<AbSlotInfo> inputs = m_editor->abSelectInputs();
        QCOMPARE(inputs.size(), 2);
        for (const AbSlotInfo &info : inputs) {
            const Qt::Key key = mgr.bindingForSlot(info.ref);
            QVERIFY(HotkeyManager::isBindableKey(key));
            QCOMPARE(m_editor->abSlotHotkeyLabel(info.ref), QKeySequence(key).toString());
        }
    }

    void sessionJsonRoundTrip() {
        m_editor->restoreState(makeGraph());
        const QVector<AbSlotInfo> inputs = m_editor->abSelectInputs();

        HotkeyManager mgr(m_parent, m_editor);
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
    QApplication app(argc, argv);
    TestHotkeys tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_hotkeys.moc"
