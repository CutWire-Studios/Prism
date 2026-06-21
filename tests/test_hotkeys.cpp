#include "ui/hotkeys/HotkeyManager.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "core/sources/SourceDescriptor.h"

#include <QtTest>
#include <QApplication>
#include <QSignalSpy>

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

    SourceDescriptor canvasDesc(const QString &name) const {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::Canvas;
        desc.displayName = name;
        desc.canvasFill = SourceDescriptor::CanvasFill::Checkered;
        return desc;
    }

    NodeId addNode(const QString &name) {
        ClipNodeModel *node = m_editor->addSourceNode(canvasDesc(name), QPixmap());
        return node ? node->nodeId() : 0;
    }

    void assignAndRelease() {
        HotkeyManager mgr(m_parent, m_editor);
        const NodeId n1 = addNode(QStringLiteral("Clip A"));
        const NodeId n2 = addNode(QStringLiteral("Clip B"));
        QVERIFY(n1 && n2);

        mgr.assignHotkeyToNode(n1);
        const Qt::Key k1 = mgr.bindingForNode(n1);
        QVERIFY(HotkeyManager::isBindableKey(k1));
        QCOMPARE(mgr.nodeForKey(k1), n1);

        mgr.assignHotkeyToNode(n2);
        const Qt::Key k2 = mgr.bindingForNode(n2);
        QVERIFY(k2 != k1);

        QVERIFY(!mgr.setBinding(n1, k2));

        mgr.releaseHotkeyForNode(n1);
        QCOMPARE(mgr.bindingForNode(n1), Qt::Key_unknown);
        QVERIFY(!mgr.nodeForKey(k1));
        QCOMPARE(mgr.nodeForKey(k2), n2);
    }

    void isBindableKey_rejectsModifiers() {
        QVERIFY(!HotkeyManager::isBindableKey(Qt::Key_Shift));
        QVERIFY(!HotkeyManager::isBindableKey(Qt::Key_Control));
        QVERIFY(HotkeyManager::isBindableKey(Qt::Key_1));
    }

    void exportImportProfile() {
        HotkeyManager mgr(m_parent, m_editor);
        const NodeId n1 = addNode(QStringLiteral("Alpha"));
        mgr.assignHotkeyToNode(n1);
        const Qt::Key key = mgr.bindingForNode(n1);

        QSignalSpy changed(&mgr, &HotkeyManager::bindingsChanged);
        const QJsonObject profile = mgr.exportProfile();
        mgr.releaseHotkeyForNode(n1);
        QVERIFY(mgr.importProfile(profile));
        QCOMPARE(mgr.bindingForNode(n1), key);
        QVERIFY(changed.count() >= 1);
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    TestHotkeys tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_hotkeys.moc"
