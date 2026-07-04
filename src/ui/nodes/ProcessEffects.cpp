#include "ui/nodes/ProcessEffects.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/canvas/CropSelectorWidget.h"
#include "core/sources/MediaSource.h"
#ifdef PRISM_HAVE_SEGMENTATION
#include "core/sources/SegmentationSource.h"
#endif

#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QJsonDocument>
#include <QVBoxLayout>

namespace {

ProcessEffectDescriptor makeCrop() {
    ProcessEffectDescriptor d;
    d.id = 0;
    d.name = "Crop";
    d.menuLabel = "Crop";
    d.defaultParams = QJsonObject{{"x", 0.0}, {"y", 0.0}, {"w", 1.0}, {"h", 1.0}};
    d.fold = [](ResolvedLayer &l, const QJsonObject &p) {
        const float nx = (float)p["x"].toDouble(0.0);
        const float ny = (float)p["y"].toDouble(0.0);
        const float nw = (float)p["w"].toDouble(1.0);
        const float nh = (float)p["h"].toDouble(1.0);
        // The rect is authored on the flipped image, so mirror it into pre-flip space.
        const float ax = l.flipH ? (1.f - nx - nw) : nx;
        const float ay = l.flipV ? (1.f - ny - nh) : ny;
        l.cropX = l.cropX + ax * l.cropW;
        l.cropY = l.cropY + ay * l.cropH;
        l.cropW = nw * l.cropW;
        l.cropH = nh * l.cropH;
    };
    d.editLabel = "Edit Crop";
    d.editDialog = [](QWidget *parent, QJsonObject &params, const QImage &referenceFrame) {
        QDialog dialog(parent);
        dialog.setWindowTitle("Edit Crop");
        auto *layout = new QVBoxLayout(&dialog);
        auto *selector = new CropSelectorWidget(&dialog);
        if (!referenceFrame.isNull())
            selector->setFrame(referenceFrame);
        selector->setCrop((float)params["x"].toDouble(0.0), (float)params["y"].toDouble(0.0),
                          (float)params["w"].toDouble(1.0), (float)params["h"].toDouble(1.0));
        layout->addWidget(selector);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        if (dialog.exec() != QDialog::Accepted) return false;
        params["x"] = (double)selector->cropX();
        params["y"] = (double)selector->cropY();
        params["w"] = (double)selector->cropW();
        params["h"] = (double)selector->cropH();
        return true;
    };
    return d;
}

ProcessEffectDescriptor makeFlipH() {
    ProcessEffectDescriptor d;
    d.id = 1;
    d.name = "Flip H";
    d.menuLabel = "Flip Horizontal";
    d.fold = [](ResolvedLayer &l, const QJsonObject &) { l.flipH = !l.flipH; };
    return d;
}

ProcessEffectDescriptor makeFlipV() {
    ProcessEffectDescriptor d;
    d.id = 2;
    d.name = "Flip V";
    d.menuLabel = "Flip Vertical";
    d.fold = [](ResolvedLayer &l, const QJsonObject &) { l.flipV = !l.flipV; };
    return d;
}

ProcessEffectDescriptor makeSegment() {
    ProcessEffectDescriptor d;
    d.id = 3;
    d.name = "Remove BG";
    d.menuLabel = "Remove Background";
    d.isDecorator = true;
#ifdef PRISM_HAVE_SEGMENTATION
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<SegmentationSource>(std::move(inner));
    };
#else
    d.available = false;
#endif
    return d;
}

} // namespace

namespace ProcessEffects {

const QVector<ProcessEffectDescriptor> &all() {
    static const QVector<ProcessEffectDescriptor> registry{
        makeCrop(), makeFlipH(), makeFlipV(), makeSegment()};
    return registry;
}

const ProcessEffectDescriptor *byId(int id) {
    for (const ProcessEffectDescriptor &d : all())
        if (d.id == id) return &d;
    return nullptr;
}

std::unique_ptr<MediaSource> applySourceEffects(std::unique_ptr<MediaSource> source,
                                                const QVector<SourceEffectRef> &effects) {
    for (const SourceEffectRef &ref : effects) {
        const ProcessEffectDescriptor *d = byId(ref.effectId);
        if (source && d && d->wrapSource)
            source = d->wrapSource(std::move(source), ref.params);
    }
    return source;
}

QString sourceEffectsKey(const QVector<SourceEffectRef> &effects) {
    QString key;
    for (const SourceEffectRef &ref : effects)
        key += QStringLiteral("%1{%2}").arg(ref.effectId)
                   .arg(QString::fromUtf8(QJsonDocument(ref.params).toJson(QJsonDocument::Compact)));
    return key;
}

} // namespace ProcessEffects
