#include "ui/common/ThumbHelper.h"
#include "core/sources/SourceDescriptor.h"
#include "ui/editors/ShapeEditDialog.h"
#include "ui/editors/TextEditDialog.h"
#include "core/sources/TextSource.h"
#include "core/text/TextAnimationPreset.h"
#include "ui/editors/SvgTemplateDialog.h"
#include "ui/editors/style/ShadingLayerStackEditor.h"
#include "core/sources/ShapeSource.h"

#include <QtTest>
#include <QApplication>
#include <QTabWidget>
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

    void makeSvgTemplateThumb_rendersBuiltin() {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::SvgTemplate;
        desc.svgTemplateId = QStringLiteral("clock");
        const QPixmap pix = ThumbHelper::makeSvgTemplateThumb(desc, 110, 65);
        QCOMPARE(pix.size(), QSize(110, 65));
        QVERIFY(!pix.isNull());
    }

    void svgTemplateDialog_roundTripsOverrides() {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::SvgTemplate;
        desc.svgTemplateId = QStringLiteral("lower_third");
        desc.svgParamsJson = QStringLiteral("{\"name\":\"Ada\"}");
        SvgTemplateDialog dlg(desc);
        const SourceDescriptor out = dlg.resultDescriptor();
        QCOMPARE(out.kind, SourceDescriptor::Kind::SvgTemplate);
        QCOMPARE(out.svgTemplateId, desc.svgTemplateId);
        QCOMPARE(out.svgParamsJson, desc.svgParamsJson);
    }

    void shapeEditDialog_roundTripsAndEditsLayers() {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::Shape;
        desc.shapeStyleJson = ShapeSource::styleToJson(prism::ShapeStyle{});
        ShapeEditDialog dlg(desc);
        QCOMPARE(dlg.resultDescriptor().shapeStyleJson, desc.shapeStyleJson);

        auto *stack = dlg.findChild<style::ShadingLayerStackEditor *>();
        QVERIFY(stack);
        const int before = ShapeSource::styleFromDescriptor(desc).layers.size();
        stack->addLayer(prism::TextLayerKind::Glow);
        const prism::ShapeStyle out = ShapeSource::styleFromDescriptor(dlg.resultDescriptor());
        QCOMPARE(out.layers.size(), before + 1);
        QCOMPARE(out.layers.last().kind, prism::TextLayerKind::Glow);
    }

    void textEditDialog_appliesPackAndAnimationAndRoundTrips() {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::Text;
        desc.textTemplate = QStringLiteral("Hello");
        TextEditDialog dlg(desc);

        auto *tabs = dlg.findChild<QTabWidget *>();
        QVERIFY(tabs);
        for (int i = 1; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            QTest::qWait(150);
        }

        const QString packId = prism::textPresets().first().id;
        dlg.applyStylePack(packId);
        QCOMPARE(dlg.style().packId, packId);

        const auto inPresets = prism::TextAnimationPresetCatalog::instance().presetsFor(prism::TextAnimSlotKind::In);
        QVERIFY(!inPresets.isEmpty());
        dlg.applyAnimationPreset(0, inPresets.first().id);
        QCOMPARE(dlg.style().animation.in.presetId, inPresets.first().id);
        QVERIFY(dlg.style().packId.isEmpty());

        const prism::TextStyle out = TextSource::styleFromDescriptor(dlg.resultDescriptor());
        QCOMPARE(out.animation.in.presetId, inPresets.first().id);
        QCOMPARE(out.layers.size(), prism::textPresets().first().style.layers.size());
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    TestThumbHelper tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_thumbhelper.moc"
