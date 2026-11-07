#include "app/common/ThumbHelper.h"
#include "app/graph/ThumbScheduler.h"
#include "core/sources/SourceDescriptor.h"

#include <QSet>
#include <QtTest>
#include <QGuiApplication>
#include <QPainter>

class TestThumbHelper : public QObject {
    Q_OBJECT

private slots:
    void makeIconThumb_sizeAndContent() {
        const QImage pix = ThumbHelper::makeIconThumb(QStringLiteral("A"), 110, 65);
        QCOMPARE(pix.size(), QSize(110, 65));
        QVERIFY(!pix.isNull());
    }

    void makeCanvasThumb_checkeredAndTransparent() {
        const QImage checkered = ThumbHelper::makeCanvasThumb(
            QStringLiteral("CV"), SourceDescriptor::CanvasFill::Checkered);
        QCOMPARE(checkered.size(), QSize(110, 65));
        QVERIFY(!checkered.isNull());

        const QImage transparent = ThumbHelper::makeCanvasThumb(
            QStringLiteral("ignored"), SourceDescriptor::CanvasFill::Transparent);
        QVERIFY(!transparent.isNull());
    }

    void makeCanvasThumb_solidColor() {
        const QColor fill(Qt::red);
        const QImage pix = ThumbHelper::makeCanvasThumb(
            QStringLiteral("CV"), SourceDescriptor::CanvasFill::Color, fill, 32, 32);
        QCOMPARE(pix.size(), QSize(32, 32));
        const QImage &img = pix;
        QCOMPARE(img.pixelColor(16, 16), fill);
    }

    void makeShaderThumb_returnsImage() {
        const QString code = QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "  fragColor = vec4(fragCoord / iResolution.xy, 0.5, 1.0);\n"
            "}\n");
        const QImage pix = ThumbHelper::makeShaderThumb(code, 64, 48);
        QCOMPARE(pix.size(), QSize(64, 48));
        QVERIFY(!pix.isNull());
    }

    void makeShaderThumb_emptyCodeFallsBack() {
        const QImage pix = ThumbHelper::makeShaderThumb(QString(), 110, 65);
        QCOMPARE(pix.size(), QSize(110, 65));
        QVERIFY(!pix.isNull());
    }

    void makeSvgTemplateThumb_rendersBuiltin() {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::SvgTemplate;
        desc.svgTemplateId = QStringLiteral("clock");
        const QImage pix = ThumbHelper::makeSvgTemplateThumb(desc, 110, 65);
        QCOMPARE(pix.size(), QSize(110, 65));
        QVERIFY(!pix.isNull());
    }

    void scheduler_roundRobinCoversEveryVisibleNodeWithinCeilNOverBudget() {
        for (int n : {1, 3, 4, 5, 9, 10, 17}) {
            ThumbScheduler sched;
            QVector<NodeId> ids;
            for (int i = 1; i <= n; ++i)
                ids.append(NodeId(i * 10));
            sched.setVisible(ids);

            QSet<NodeId> seen;
            const int ticks = (n + ThumbScheduler::kBudget - 1) / ThumbScheduler::kBudget;
            for (int t = 0; t < ticks; ++t) {
                const QVector<NodeId> batch = sched.nextBatch();
                QVERIFY(batch.size() <= ThumbScheduler::kBudget);
                QCOMPARE(batch.size(), qMin(n, ThumbScheduler::kBudget));
                for (NodeId id : batch)
                    seen.insert(id);
            }
            QCOMPARE(seen.size(), n);
        }
    }

    void scheduler_picksNextFourInOrderAndWraps() {
        ThumbScheduler sched;
        sched.setVisible({1, 2, 3, 4, 5, 6});
        QCOMPARE(sched.nextBatch(), (QVector<NodeId>{1, 2, 3, 4}));
        QCOMPARE(sched.nextBatch(), (QVector<NodeId>{5, 6, 1, 2}));
        QCOMPARE(sched.nextBatch(), (QVector<NodeId>{3, 4, 5, 6}));
    }

    void scheduler_skipsInvisibleNodes() {
        ThumbScheduler sched;
        sched.setVisible({1, 2, 3, 4, 5, 6});
        sched.nextBatch();
        sched.setVisible({3, 5, 6});
        QSet<NodeId> seen;
        for (int t = 0; t < 2; ++t)
            for (NodeId id : sched.nextBatch())
                seen.insert(id);
        QCOMPARE(seen, (QSet<NodeId>{3, 5, 6}));
        sched.setVisible({});
        QVERIFY(sched.nextBatch().isEmpty());
    }

    void scheduler_resumesAfterLastServedWhenSetChanges() {
        ThumbScheduler sched;
        sched.setVisible({1, 2, 3, 4, 5, 6, 7, 8});
        QCOMPARE(sched.nextBatch(), (QVector<NodeId>{1, 2, 3, 4}));
        sched.setVisible({2, 4, 5, 6, 7, 8});
        QCOMPARE(sched.nextBatch(), (QVector<NodeId>{5, 6, 7, 8}));
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    TestThumbHelper tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_thumbhelper.moc"
