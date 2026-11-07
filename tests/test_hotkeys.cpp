#include "app/hotkeys/AppShortcuts.h"
#include "app/hotkeys/HotkeyManager.h"
#include "app/show/ShowManager.h"

#include <QtTest>
#include <QGuiApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonObject>

class TestHotkeys : public QObject {
    Q_OBJECT

    ShowManager *m_show = nullptr;

private slots:
    void init() {
        QSettings().clear();
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

    void suspendedDoesNotEmitScene() {
        HotkeyManager mgr(m_show);
        QSignalSpy spy(&mgr, &HotkeyManager::sceneTriggered);
        QVERIFY(mgr.setBinding(idAt(1), Qt::Key_G));

        QWindow window;
        mgr.setSuspended(true);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &press);
        QCOMPARE(spy.size(), 0);

        mgr.setSuspended(false);
        QKeyEvent again(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &again);
        QCOMPARE(spy.size(), 1);
    }

    void reservedCombosMatchModifiers() {
        const QVariantList reserved = {int(Qt::Key_M), int(Qt::Key_F), int(Qt::Key_F | Qt::ShiftModifier)};
        QVERIFY(HotkeyManager::isReserved(Qt::Key_M, reserved));
        QVERIFY(!HotkeyManager::isReserved(Qt::Key_M | Qt::ShiftModifier, reserved));
        QVERIFY(HotkeyManager::isReserved(Qt::Key_F | Qt::ShiftModifier, reserved));
        QVERIFY(!HotkeyManager::isReserved(Qt::Key_G, reserved));
        QVERIFY(!HotkeyManager::isReserved(Qt::Key_M, {}));
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

    void appActionDefaultsResolve() {
        HotkeyManager mgr(m_show);
        const AppShortcuts *a = mgr.appShortcuts();
        QCOMPARE(a->actions().size(), 16);
        QCOMPARE(a->sequences(QStringLiteral("panic.blackout")), QStringList{QStringLiteral("B")});
        QCOMPARE(a->sequences(QStringLiteral("transport.take")),
                 (QStringList{QStringLiteral("Return"), QStringLiteral("Enter")}));
        QCOMPARE(a->sequences(QStringLiteral("scene.prevTab")),
                 (QStringList{QStringLiteral("Ctrl+Shift+Tab"), QStringLiteral("Ctrl+Backtab")}));
        QVERIFY(!a->sequences(QStringLiteral("edit.undo")).isEmpty());
        for (const AppShortcuts::Action &act : a->actions())
            QVERIFY2(!a->sequences(act.id).isEmpty(), qPrintable(act.id));
    }

    void rebindingChangesSequencesAndNotifies() {
        HotkeyManager mgr(m_show);
        QSignalSpy spy(mgr.appShortcuts(), &AppShortcuts::changed);
        QVERIFY(mgr.setActionSequence(QStringLiteral("file.save"), QKeySequence(QStringLiteral("Ctrl+Alt+S"))).isEmpty());
        QCOMPARE(mgr.appShortcuts()->sequences(QStringLiteral("file.save")), QStringList{QStringLiteral("Ctrl+Alt+S")});
        QCOMPARE(mgr.appShortcuts()->sequenceMap().value(QStringLiteral("file.save")).toStringList(),
                 QStringList{QStringLiteral("Ctrl+Alt+S")});
        QCOMPARE(spy.size(), 1);
    }

    void appVsAppConflictRefused() {
        HotkeyManager mgr(m_show);
        const QStringList before = mgr.appShortcuts()->sequences(QStringLiteral("panic.freeze"));
        QVERIFY(!mgr.setActionSequence(QStringLiteral("panic.freeze"), QKeySequence(QStringLiteral("B"))).isEmpty());
        QCOMPARE(mgr.appShortcuts()->sequences(QStringLiteral("panic.freeze")), before);
    }

    void appVsSceneConflictRefusedBothWays() {
        HotkeyManager mgr(m_show);
        QVERIFY(mgr.setBinding(idAt(0), Qt::Key_G));
        QVERIFY(!mgr.setActionSequence(QStringLiteral("panic.freeze"), QKeySequence(QStringLiteral("G"))).isEmpty());
        QVERIFY(!mgr.setActionSequence(QStringLiteral("panic.freeze"), QKeySequence(QStringLiteral("Shift+G"))).isEmpty());
        QVERIFY(mgr.setActionSequence(QStringLiteral("panic.freeze"), QKeySequence(QStringLiteral("Ctrl+G"))).isEmpty());

        QVERIFY(!mgr.setBinding(idAt(1), Qt::Key_B));
        QVERIFY(mgr.setBinding(idAt(1), Qt::Key_P));   // freed by the rebind above
    }

    void autoAssignSkipsAppKeys() {
        for (int i = 0; i < 40; ++i)
            m_show->addScene(QStringLiteral("S%1").arg(i));
        HotkeyManager mgr(m_show);
        mgr.syncWithShow();
        QVERIFY(mgr.sceneForKey(Qt::Key_B).isEmpty());
        QVERIFY(mgr.sceneForKey(Qt::Key_P).isEmpty());
        QVERIFY(!mgr.sceneForKey(Qt::Key_G).isEmpty());
    }

    void legacySceneKeyOnAppKeyIsDropped() {
        HotkeyManager mgr(m_show);
        QJsonObject hk;
        hk["scene"] = idAt(0);
        hk["key"] = int(Qt::Key_B);
        mgr.restoreHotkeys(QJsonArray{hk});
        QVERIFY(mgr.bindingForScene(idAt(0)) != Qt::Key_B);
        QVERIFY(HotkeyManager::isBindableKey(mgr.bindingForScene(idAt(0))));
    }

    void profileRoundTripIncludesOverrides() {
        HotkeyManager mgr(m_show);
        QVERIFY(mgr.setActionSequence(QStringLiteral("edit.undo"), QKeySequence(QStringLiteral("Ctrl+Alt+Z"))).isEmpty());
        const QJsonObject profile = mgr.exportProfile();
        QCOMPARE(profile.value("actions").toObject().value("edit.undo").toArray().first().toString(),
                 QStringLiteral("Ctrl+Alt+Z"));

        mgr.resetAllActions();
        QVERIFY(!mgr.appShortcuts()->isOverridden(QStringLiteral("edit.undo")));
        QVERIFY(mgr.importProfile(profile));
        QCOMPARE(mgr.appShortcuts()->sequences(QStringLiteral("edit.undo")), QStringList{QStringLiteral("Ctrl+Alt+Z")});

        // Old profile without "actions" imports and leaves overrides alone; unknown ids are ignored.
        QJsonObject old = profile;
        old.remove("actions");
        QVERIFY(mgr.importProfile(old));
        QVERIFY(mgr.appShortcuts()->isOverridden(QStringLiteral("edit.undo")));
        QJsonObject unknown = profile;
        unknown.insert("actions", QJsonObject{{"no.such", QJsonArray{"X"}}});
        QVERIFY(mgr.importProfile(unknown));
        QVERIFY(!mgr.appShortcuts()->isOverridden(QStringLiteral("edit.undo")));
        QVERIFY(!mgr.appShortcuts()->isOverridden(QStringLiteral("no.such")));
    }

    void resetToDefault() {
        HotkeyManager mgr(m_show);
        const QStringList def = mgr.appShortcuts()->sequences(QStringLiteral("transport.take"));
        QVERIFY(mgr.setActionSequence(QStringLiteral("transport.take"), QKeySequence(QStringLiteral("Ctrl+T"))).isEmpty());
        QVERIFY(mgr.setActionSequence(QStringLiteral("transport.cut"), QKeySequence(QStringLiteral("Ctrl+K"))).isEmpty());
        QVERIFY(mgr.resetAction(QStringLiteral("transport.take")).isEmpty());
        QCOMPARE(mgr.appShortcuts()->sequences(QStringLiteral("transport.take")), def);
        QVERIFY(mgr.appShortcuts()->isOverridden(QStringLiteral("transport.cut")));
        mgr.resetAllActions();
        QVERIFY(!mgr.appShortcuts()->isOverridden(QStringLiteral("transport.cut")));
    }
};

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    TestHotkeys tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_hotkeys.moc"
