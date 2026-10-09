#include "core/render/SkiaRender.h"
#include "core/sources/ShapeSource.h"
#include "core/sources/SvgTemplateSource.h"
#include "core/sources/SvgTemplates.h"
#include "core/sources/TextSource.h"
#include "core/text/ShapeStyle.h"
#include "core/text/TextAnimationPreset.h"
#include "core/text/TextStyle.h"

#include <QGuiApplication>
#include <QtTest>

namespace {

int inkPixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x)
            count += row[x * 4 + 3] > 0 ? 1 : 0;
    }
    return count;
}

} // namespace

class TestSkiaRender : public QObject {
    Q_OBJECT

private slots:
    void bundledFontsResolveForSvgText() {
        for (const char *family : {"JetBrains Mono", "Inter", "Montserrat", "Bebas Neue", "Anton", "Playfair Display"})
            QVERIFY2(prism::svgFontAvailable(QString::fromLatin1(family)), family);
        QVERIFY(!prism::svgFontAvailable(QStringLiteral("No Such Family 12345")));
    }

    void presetsLoadFromResources() {
        QVERIFY(prism::TextAnimationPresetCatalog::instance().presets().size() > 40);
    }

    void everyStylePackRenders() {
        QVERIFY(!prism::textPresets().isEmpty());
        prism::TextAnimClock hold;
        hold.totalSec = 4.0;
        for (const prism::TextPreset &preset : prism::textPresets()) {
            prism::TextStyle style = preset.style;
            style.pixelSize = 48;
            const QImage image = prism::renderText(style, preset.sampleText, QSize(900, 300), hold);
            QCOMPARE(image.format(), QImage::Format_RGBA8888_Premultiplied);
            QVERIFY2(inkPixels(image) > 100, qPrintable(preset.id));
        }
    }

    void inAndOutPhasesChangeThePose() {
        prism::TextStyle style = prism::textPresets().first().style;
        style.pixelSize = 48;
        prism::TextAnimationSlot in;
        in.presetId = QStringLiteral("fade");
        style.animation.in = in;
        style.animation.out = in;
        const QString text = QStringLiteral("Hello");
        const QSize canvas(600, 200);

        prism::TextAnimClock start;
        start.phase = prism::TextAnimClock::Phase::In;
        prism::TextAnimClock hold;
        hold.totalSec = prism::textAnimationInSeconds(style, text, canvas) + 1.0;
        prism::TextAnimClock endOfOut;
        endOfOut.phase = prism::TextAnimClock::Phase::Out;
        endOfOut.totalSec = hold.totalSec + 1.0;
        endOfOut.phaseElapsedSec = prism::textAnimationOutSeconds(style, text, canvas);

        QVERIFY(prism::textAnimationInSeconds(style, text, canvas) > 0.0);
        QCOMPARE(inkPixels(prism::renderText(style, text, canvas, start)), 0);
        QVERIFY(inkPixels(prism::renderText(style, text, canvas, hold)) > 100);
        QCOMPARE(inkPixels(prism::renderText(style, text, canvas, endOfOut)), 0);
    }

    void textSourceFollowsOnAirClock() {
        prism::TextStyle style;
        style.pixelSize = 48;
        prism::TextAnimationSlot slot;
        slot.presetId = QStringLiteral("fade");
        style.animation.in = slot;
        style.animation.out = slot;
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::Text;
        desc.textTemplate = QStringLiteral("Hello");
        desc.canvasWidth = 600;
        desc.canvasHeight = 200;
        desc.textStyleJson = TextSource::styleToJson(style);

        TextSource src(desc);
        using Phase = prism::TextAnimClock::Phase;
        QCOMPARE(src.animPhase(), Phase::Hold);
        QVERIFY(src.nextFrame());
        QVERIFY(!src.nextFrame());
        QCOMPARE(src.requestOut(), 0.0);

        src.setOnAir(true);
        QCOMPARE(src.animPhase(), Phase::In);
        QVERIFY(src.nextFrame());

        const double outSecs = src.requestOut();
        QVERIFY(outSecs > 0.0);
        QCOMPARE(src.animPhase(), Phase::Out);
        QVERIFY(!src.outFinished());
        QTest::qWait(int(outSecs * 1000) + 100);
        QVERIFY(src.outFinished());
        QVERIFY(src.nextFrame());
        QVERIFY(!src.nextFrame());

        src.setOnAir(false);
        QCOMPARE(src.animPhase(), Phase::Hold);
    }

    void shapeSourceRendersAndIsStatic() {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::Shape;
        desc.canvasWidth = 400;
        desc.canvasHeight = 300;
        desc.shapeStyleJson = ShapeSource::styleToJson(prism::ShapeStyle{});
        ShapeSource src(desc);
        QCOMPARE(src.frameSize(), QSize(400, 300));
        QVERIFY(inkPixels(QImage(src.frameData(), 400, 300, QImage::Format_RGBA8888)) > 1000);
        QVERIFY(src.nextFrame());
        QVERIFY(!src.nextFrame());
    }

    void svgRendersAtIntrinsicSize() {
        const QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' width='120' height='40'>"
                               "<rect width='60' height='40' fill='#f00'/></svg>";
        QCOMPARE(prism::svgIntrinsicSize(svg), QSizeF(120, 40));
        const QImage image = prism::renderSvg(svg);
        QCOMPARE(image.size(), QSize(120, 40));
        QCOMPARE(qAlpha(image.pixel(10, 10)), 255);
        QCOMPARE(qAlpha(image.pixel(100, 10)), 0);
        QCOMPARE(prism::renderSvg(svg, 2.0).size(), QSize(240, 80));
        QVERIFY(prism::renderSvg("not svg").isNull());
    }

    void everyBuiltinTemplateRendersWithText() {
        const auto templates = prism::builtinSvgTemplates();
        QCOMPARE(templates.size(), 7);
        for (const prism::SvgTemplateInfo &info : templates) {
            QVERIFY2(!info.params.isEmpty(), qPrintable(info.id));
            SourceDescriptor desc;
            desc.kind = SourceDescriptor::Kind::SvgTemplate;
            desc.svgTemplateId = info.id;
            const QImage frame = SvgTemplateSource::renderDescriptor(desc);
            QVERIFY2(!frame.isNull(), qPrintable(info.id));
            QCOMPARE(frame.format(), QImage::Format_RGBA8888);
            QVERIFY2(inkPixels(frame) > 500, qPrintable(info.id));
        }
    }

    void svgTokensAreXmlEscaped() {
        const QString out = prism::substituteSvgTokens(QStringLiteral("<text a=\"{a}\">{b}</text>{missing}"),
                                                       {{"a", "x\"y"}, {"b", "A & <B>"}});
        QCOMPARE(out, QStringLiteral("<text a=\"x&quot;y\">A &amp; &lt;B&gt;</text>"));
    }

    void svgParamsOverrideDefaultsAndScriptDataOverridesBoth() {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::SvgTemplate;
        desc.svgTemplateId = QStringLiteral("live_bug");
        const QImage base = SvgTemplateSource::renderDescriptor(desc);
        desc.svgParamsJson = SvgTemplateSource::paramsToJson({{"color", "#0000ff"}});
        const QImage blue = SvgTemplateSource::renderDescriptor(desc);
        QVERIFY(base.pixel(4, 32) != blue.pixel(4, 32));
        QCOMPARE(QColor(blue.pixel(4, 32)), QColor(0, 0, 255));
        const QImage green = SvgTemplateSource::renderDescriptor(desc, {{"color", "#00ff00"}});
        QCOMPARE(QColor(green.pixel(4, 32)), QColor(0, 255, 0));
    }

    void svgSourceRefreshesOnDataAndClockTokens() {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::SvgTemplate;
        desc.svgTemplateId = QStringLiteral("countdown_timer");
        desc.svgParamsJson = SvgTemplateSource::paramsToJson({{"target", "5"}});
        SvgTemplateSource src(desc);
        QVERIFY(src.isReady());
        QVERIFY(src.hasAlpha());
        QVERIFY(!src.nextFrame());
        QTest::qWait(1100);
        QVERIFY(src.nextFrame());

        auto data = std::make_shared<ScriptOutput>();
        data->json = QStringLiteral("{\"label\":\"X\"}");
        data->version.store(1);
        src.setDataSource(data);
        QVERIFY(src.nextFrame());
    }

    void svgSourceIsStaticWithoutBuiltinTokens() {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::SvgTemplate;
        desc.svgTemplateId = QStringLiteral("lower_third");
        SvgTemplateSource src(desc);
        QVERIFY(src.isReady());
        QTest::qWait(1100);
        QVERIFY(!src.nextFrame());
    }

    void shapeRenders() {
        prism::ShapeStyle shape;
        const QImage image = prism::renderShape(shape, QSize(200, 200), QRectF(40, 40, 120, 120), 0.0);
        QCOMPARE(image.size(), QSize(200, 200));
        QVERIFY(inkPixels(image) > 1000);
        QCOMPARE(qAlpha(image.pixel(2, 2)), 0);
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestSkiaRender test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_skiarender.moc"
