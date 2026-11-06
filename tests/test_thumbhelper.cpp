#include "app/common/ThumbHelper.h"
#include "core/sources/SourceDescriptor.h"

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
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    TestThumbHelper tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_thumbhelper.moc"
