#include "ui/common/ThumbHelper.h"
#include "core/sources/SourceDescriptor.h"

#include <QtTest>
#include <QApplication>
#include <QPainter>

class TestThumbHelper : public QObject {
    Q_OBJECT

private slots:
    void makeIconThumb_sizeAndContent() {
        const QPixmap pix = ThumbHelper::makeIconThumb(QStringLiteral("A"), 110, 65);
        QCOMPARE(pix.size(), QSize(110, 65));
        QVERIFY(!pix.isNull());
    }

    void makeCanvasThumb_checkeredAndTransparent() {
        const QPixmap checkered = ThumbHelper::makeCanvasThumb(
            QStringLiteral("CV"), SourceDescriptor::CanvasFill::Checkered);
        QCOMPARE(checkered.size(), QSize(110, 65));
        QVERIFY(!checkered.isNull());

        const QPixmap transparent = ThumbHelper::makeCanvasThumb(
            QStringLiteral("ignored"), SourceDescriptor::CanvasFill::Transparent);
        QVERIFY(!transparent.isNull());
    }

    void makeCanvasThumb_solidColor() {
        const QColor fill(Qt::red);
        const QPixmap pix = ThumbHelper::makeCanvasThumb(
            QStringLiteral("CV"), SourceDescriptor::CanvasFill::Color, fill, 32, 32);
        QCOMPARE(pix.size(), QSize(32, 32));
        QImage img = pix.toImage();
        QCOMPARE(img.pixelColor(16, 16), fill);
    }

    void makeShaderThumb_returnsPixmap() {
        const QString code = QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "  fragColor = vec4(fragCoord / iResolution.xy, 0.5, 1.0);\n"
            "}\n");
        const QPixmap pix = ThumbHelper::makeShaderThumb(code, 64, 48);
        QCOMPARE(pix.size(), QSize(64, 48));
        QVERIFY(!pix.isNull());
    }

    void makeShaderThumb_emptyCodeFallsBack() {
        const QPixmap pix = ThumbHelper::makeShaderThumb(QString(), 110, 65);
        QCOMPARE(pix.size(), QSize(110, 65));
        QVERIFY(!pix.isNull());
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    TestThumbHelper tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_thumbhelper.moc"
