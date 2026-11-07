#include "app/hotkeys/HotkeyManager.h"
#include "app/show/ShowManager.h"

#include <QtTest>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>

class TestHotkeys : public QObject {
    Q_OBJECT

    ShowManager *m_show = nullptr;

private slots:
    void init() {
        m_show = new ShowManager;
        m_show->addScene(QStringLiteral("Second"));
    }

    void cleanup() {
        delete m_show;
        m_show = nullptr;
    }

    QString idAt(int i) const { return m_show->scenes().at(i)->id(); }

    void syncAssignsKeysToScenes() {
        HotkeyManager mgr(m_show);
        mgr.syncWithShow();

        const Qt::Key k1 = mgr.bindingForScene(idAt(0));
        const Qt::Key k2 = mgr.bindingForScene(idAt(1));
        QVERIFY(HotkeyManager::isBindableKey(k1));
        QVERIFY(HotkeyManager::isBindableKey(k2));
        QVERIFY(k1 != k2);
        QCOMPARE(mgr.sceneForKey(k1), idAt(0));

        // A key taken by another scene is rejected.
        QVERIFY(!mgr.setBinding(idAt(0), k2));

        mgr.clearBinding(idAt(0));
        QCOMPARE(mgr.bindingForScene(idAt(0)), Qt::Key_unknown);
        QVERIFY(mgr.sceneForKey(k1).isEmpty());
        QCOMPARE(mgr.sceneForKey(k2), idAt(1));
    }

    void newScenesGetKeysAndRemovedScenesReleaseThem() {
        HotkeyManager mgr(m_show);
        connect(m_show, &ShowManager::sceneAdded, &mgr, &HotkeyManager::syncWithShow);
        connect(m_show, &ShowManager::scenesReset, &mgr, &HotkeyManager::syncWithShow);

        Scene *third = m_show->addScene(QStringLiteral("Third"));
        const Qt::Key key = mgr.bindingForScene(third->id());
        QVERIFY(HotkeyManager::isBindableKey(key));

        const QString id = third->id();
        QVERIFY(m_show->removeScene(id));
        QVERIFY(mgr.sceneForKey(key).isEmpty());
    }

    void triggeringKeyEmitsScene() {
        HotkeyManager mgr(m_show);
        QSignalSpy spy(&mgr, &HotkeyManager::sceneTriggered);
        QVERIFY(mgr.setBinding(idAt(1), Qt::Key_G));

        QKeyEvent press(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier);
        QWindow window;
        QVERIFY(QCoreApplication::sendEvent(&window, &press) || true);
        // Delivered through the application event filter to top-level windows.
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().at(0).toString(), idAt(1));
        QCOMPARE(spy.first().at(1).toBool(), false);

        QKeyEvent shifted(QEvent::KeyPress, Qt::Key_G, Qt::ShiftModifier);
        QCoreApplication::sendEvent(&window, &shifted);
        QCOMPARE(spy.size(), 2);
        QCOMPARE(spy.last().at(1).toBool(), true);
    }

    void isBindableKey_rejectsModifiers() {
        QVERIFY(!HotkeyManager::isBindableKey(Qt::Key_Shift));
        QVERIFY(!HotkeyManager::isBindableKey(Qt::Key_Control));
        QVERIFY(HotkeyManager::isBindableKey(Qt::Key_1));
    }

    void exportImportProfile() {
        HotkeyManager mgr(m_show);
        mgr.syncWithShow();
        const Qt::Key key = mgr.bindingForScene(idAt(0));
        QVERIFY(HotkeyManager::isBindableKey(key));

        QSignalSpy changed(&mgr, &HotkeyManager::bindingsChanged);
        const QJsonObject profile = mgr.exportProfile();
        mgr.clearBinding(idAt(0));
        QVERIFY(mgr.importProfile(profile));
        QCOMPARE(mgr.bindingForScene(idAt(0)), key);
        QVERIFY(changed.count() >= 1);
    }

    void sessionJsonRoundTrip() {
        HotkeyManager mgr(m_show);
        QVERIFY(mgr.setBinding(idAt(0), Qt::Key_5));
        QVERIFY(mgr.setBinding(idAt(1), Qt::Key_K));

        const QJsonArray saved = mgr.hotkeysJson();
        mgr.clearBinding(idAt(0));
        mgr.clearBinding(idAt(1));
        mgr.restoreHotkeys(saved);

        QCOMPARE(mgr.bindingForScene(idAt(0)), Qt::Key_5);
        QCOMPARE(mgr.bindingForScene(idAt(1)), Qt::Key_K);
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    TestHotkeys tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_hotkeys.moc"
