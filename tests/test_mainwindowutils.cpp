#include "ui/mainwindow/MainWindowUtils.h"
#include "ui/session/SessionManager.h"

#include <QtTest>

class TestMainWindowUtils : public QObject {
    Q_OBJECT

private slots:
    void formatRecordingElapsed_zero() {
        QCOMPARE(MainWindowUtils::formatRecordingElapsed(0), QStringLiteral("00:00:00"));
    }

    void formatRecordingElapsed_underOneHour() {
        QCOMPARE(MainWindowUtils::formatRecordingElapsed(125000), QStringLiteral("00:02:05"));
    }

    void formatRecordingElapsed_oneHourOrMore() {
        QCOMPARE(MainWindowUtils::formatRecordingElapsed(3661000), QStringLiteral("01:01:01"));
    }

    void ensureExtension_missing() {
        const QString ext = QString::fromUtf8(SessionManager::kSessionExtension);
        QCOMPARE(MainWindowUtils::ensureExtension(QStringLiteral("/tmp/session"), ext),
                 QStringLiteral("/tmp/session") + ext);
    }

    void ensureExtension_present() {
        const QString ext = QString::fromUtf8(SessionManager::kSessionExtension);
        const QString path = QStringLiteral("/tmp/session") + ext;
        QCOMPARE(MainWindowUtils::ensureExtension(path, ext), path);
    }

    void ensureExtension_differentCase() {
        const QString ext = QString::fromUtf8(SessionManager::kSessionExtension);
        const QString path = QStringLiteral("/tmp/session") + ext.toUpper();
        QCOMPARE(MainWindowUtils::ensureExtension(path, ext), path);
    }

    void diffNewItems() {
        const QStringList before = {QStringLiteral("a.mp4"), QStringLiteral("b.mp4")};
        const QStringList after  = {QStringLiteral("a.mp4"), QStringLiteral("b.mp4"), QStringLiteral("c.mp4")};
        QCOMPARE(MainWindowUtils::diffNewItems(before, after),
                 QStringList{QStringLiteral("c.mp4")});
    }

    void diffNewItems_none() {
        const QStringList items = {QStringLiteral("a.mp4")};
        QVERIFY(MainWindowUtils::diffNewItems(items, items).isEmpty());
    }

    void panicStateForButtons() {
        using PS = MainWindowUtils::PanicState;
        QCOMPARE(MainWindowUtils::panicStateForButtons(false, false, false), PS::None);
        QCOMPARE(MainWindowUtils::panicStateForButtons(true, false, false), PS::Blackout);
        QCOMPARE(MainWindowUtils::panicStateForButtons(false, true, false), PS::StayTuned);
        QCOMPARE(MainWindowUtils::panicStateForButtons(false, false, true), PS::Freeze);
        QCOMPARE(MainWindowUtils::panicStateForButtons(true, true, true), PS::Blackout);
    }

    void isBackwardJump() {
        QVERIFY(MainWindowUtils::isBackwardJump(1.0, 5.0));
        QVERIFY(!MainWindowUtils::isBackwardJump(4.9, 5.0));
        QVERIFY(!MainWindowUtils::isBackwardJump(5.0, 5.0));
    }

    void isBackwardJump_customThreshold() {
        QVERIFY(MainWindowUtils::isBackwardJump(0.0, 1.0, 0.5));
        QVERIFY(!MainWindowUtils::isBackwardJump(0.6, 1.0, 0.5));
    }
};

QTEST_APPLESS_MAIN(TestMainWindowUtils)
#include "test_mainwindowutils.moc"
