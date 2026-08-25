#include "ui/nodes/ProcessEffects.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/canvas/CropSelectorWidget.h"
#include "ui/canvas/PerspectiveSelectorWidget.h"
#include "ui/canvas/PolygonSelectorWidget.h"
#include "core/sources/MediaSource.h"
#include "core/sources/EffectSources.h"
#ifdef PRISM_HAVE_SEGMENTATION
#include "core/sources/SegmentationSource.h"
#endif

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {

// ── JSON helpers for polygon / corner params ────────────────────────────────
QVector<QPointF> pointsFromArray(const QJsonArray &arr) {
    QVector<QPointF> pts;
    for (const QJsonValue &v : arr) {
        const QJsonArray pair = v.toArray();
        if (pair.size() == 2)
            pts << QPointF(pair.at(0).toDouble(), pair.at(1).toDouble());
    }
    return pts;
}

QJsonArray pointsToArray(const QVector<QPointF> &pts) {
    QJsonArray arr;
    for (const QPointF &p : pts)
        arr.append(QJsonArray{p.x(), p.y()});
    return arr;
}

QVector<QPointF> cornersFromParams(const QJsonObject &p) {
    QVector<QPointF> c = pointsFromArray(p["corners"].toArray());
    if (c.size() != 4) c = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    return c;
}

// A framed reference preview plus OK/Cancel; returns the dialog's exec() result.
// The @p build callback populates the form area with effect-specific controls.
// The optional @p collect callback runs on acceptance while the dialog (and its
// child widgets) are still alive — read widget state there, not after this
// function returns, since the QDialog and its children are destroyed on return.
bool runEffectDialog(QWidget *parent, const QString &title,
                     const std::function<void(QDialog *, QVBoxLayout *)> &build,
                     const std::function<void()> &collect = {}) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    build(&dialog, layout);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (accepted && collect) collect();
    return accepted;
}

// ── Crop (fold) ──────────────────────────────────────────────────────────────
ProcessEffectDescriptor makeCrop() {
    ProcessEffectDescriptor d;
    d.id = 0;
    d.name = "Crop";
    d.menuLabel = "Crop";
    d.defaultParams = QJsonObject{{"x", 0.0}, {"y", 0.0}, {"w", 1.0}, {"h", 1.0}};
    d.isDecorator = true;
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<CropSource>(std::move(inner),
                                            (float)p["x"].toDouble(0.0),
                                            (float)p["y"].toDouble(0.0),
                                            (float)p["w"].toDouble(1.0),
                                            (float)p["h"].toDouble(1.0));
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

// ── Legacy separate flips (id 1/2): kept for project load, hidden from menu ───
ProcessEffectDescriptor makeFlipH() {
    ProcessEffectDescriptor d;
    d.id = 1;
    d.name = "Flip H";
    d.menuLabel = "Flip Horizontal";
    d.available = false;   // superseded by the combined Flip node
    d.isDecorator = true;
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<FlipSource>(std::move(inner), true, false);
    };
    return d;
}

ProcessEffectDescriptor makeFlipV() {
    ProcessEffectDescriptor d;
    d.id = 2;
    d.name = "Flip V";
    d.menuLabel = "Flip Vertical";
    d.available = false;   // superseded by the combined Flip node
    d.isDecorator = true;
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<FlipSource>(std::move(inner), false, true);
    };
    return d;
}

// ── Remove Background (decorator) ────────────────────────────────────────────
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

// ── Flip (fold, combined direction) ──────────────────────────────────────────
ProcessEffectDescriptor makeFlip() {
    ProcessEffectDescriptor d;
    d.id = 4;
    d.name = "Flip";
    d.menuLabel = "Flip";
    d.defaultParams = QJsonObject{{"direction", 1}};   // 0=H, 1=V, 2=Both
    d.isDecorator = true;
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        const int dir = p["direction"].toInt(1);
        return std::make_unique<FlipSource>(std::move(inner),
                                            dir == 0 || dir == 2,
                                            dir == 1 || dir == 2);
    };
    d.editLabel = "Direction";
    d.dynamicLabel = [](const QJsonObject &p) {
        switch (p["direction"].toInt(1)) {
        case 0: return QStringLiteral("Horizontal");
        case 2: return QStringLiteral("Both");
        default: return QStringLiteral("Vertical");
        }
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const QImage &) {
        QComboBox *combo = nullptr;
        const bool ok = runEffectDialog(parent, "Flip", [&](QDialog *, QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            combo = new QComboBox;
            combo->addItems({"Horizontal", "Vertical", "Both"});
            combo->setCurrentIndex(qBound(0, params["direction"].toInt(1), 2));
            form->addRow("Direction", combo);
            layout->addLayout(form);
        }, [&]() { params["direction"] = combo->currentIndex(); });
        return ok;
    };
    return d;
}

// ── Rotate (decorator) ───────────────────────────────────────────────────────
ProcessEffectDescriptor makeRotate() {
    ProcessEffectDescriptor d;
    d.id = 5;
    d.name = "Rotate";
    d.menuLabel = "Rotate";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"angle", 90}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<RotateSource>(std::move(inner), p["angle"].toInt(90));
    };
    d.editLabel = "Angle";
    d.editDialog = [](QWidget *parent, QJsonObject &params, const QImage &) {
        QComboBox *combo = nullptr;
        const int angles[] = {0, 90, 180, 270};
        const bool ok = runEffectDialog(parent, "Rotate", [&](QDialog *, QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            combo = new QComboBox;
            combo->addItems({"0\u00B0", "90\u00B0", "180\u00B0", "270\u00B0"});
            const int cur = params["angle"].toInt(90);
            for (int i = 0; i < 4; ++i)
                if (angles[i] == cur) combo->setCurrentIndex(i);
            form->addRow("Angle", combo);
            layout->addLayout(form);
        }, [&]() { params["angle"] = angles[combo->currentIndex()]; });
        return ok;
    };
    return d;
}

// ── Opacity (decorator) ──────────────────────────────────────────────────────
ProcessEffectDescriptor makeOpacity() {
    ProcessEffectDescriptor d;
    d.id = 6;
    d.name = "Opacity";
    d.menuLabel = "Opacity";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"opacity", 1.0}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<OpacitySource>(std::move(inner), p["opacity"].toDouble(1.0));
    };
    d.editLabel = "Opacity";
    d.editDialog = [](QWidget *parent, QJsonObject &params, const QImage &) {
        QSlider *slider = nullptr;
        const bool ok = runEffectDialog(parent, "Opacity", [&](QDialog *, QVBoxLayout *layout) {
            auto *row = new QHBoxLayout;
            slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 100);
            slider->setValue(qRound(params["opacity"].toDouble(1.0) * 100));
            auto *val = new QLabel(QString::number(slider->value()) + "%");
            QObject::connect(slider, &QSlider::valueChanged, val,
                             [val](int v) { val->setText(QString::number(v) + "%"); });
            row->addWidget(new QLabel("Opacity"));
            row->addWidget(slider);
            row->addWidget(val);
            layout->addLayout(row);
        }, [&]() { params["opacity"] = slider->value() / 100.0; });
        return ok;
    };
    return d;
}

// ── Chroma Key (decorator) ───────────────────────────────────────────────────
ProcessEffectDescriptor makeChromaKey() {
    ProcessEffectDescriptor d;
    d.id = 7;
    d.name = "Chroma Key";
    d.menuLabel = "Chroma Key";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"r", 0}, {"g", 255}, {"b", 0},
                                  {"threshold", 0.25}, {"smoothness", 0.10}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        const QColor key(p["r"].toInt(0), p["g"].toInt(255), p["b"].toInt(0));
        return std::make_unique<ChromaKeySource>(std::move(inner), key,
                                                 p["threshold"].toDouble(0.25),
                                                 p["smoothness"].toDouble(0.10));
    };
    d.editLabel = "Settings";
    d.editDialog = [](QWidget *parent, QJsonObject &params, const QImage &) {
        auto key = std::make_shared<QColor>(params["r"].toInt(0), params["g"].toInt(255),
                                            params["b"].toInt(0));
        QSlider *thr = nullptr;
        QSlider *soft = nullptr;
        const bool ok = runEffectDialog(parent, "Chroma Key", [&](QDialog *dlg, QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            auto *colorBtn = new QPushButton;
            colorBtn->setAutoFillBackground(true);
            auto paint = [colorBtn, key] {
                colorBtn->setText(key->name());
                colorBtn->setStyleSheet(QString("background:%1; color:%2;")
                    .arg(key->name(), key->lightness() < 128 ? "white" : "black"));
            };
            paint();
            QObject::connect(colorBtn, &QPushButton::clicked, dlg, [dlg, key, paint] {
                const QColor c = QColorDialog::getColor(*key, dlg, "Key Colour");
                if (c.isValid()) { *key = c; paint(); }
            });
            form->addRow("Key Colour", colorBtn);

            thr = new QSlider(Qt::Horizontal);
            thr->setRange(0, 100);
            thr->setValue(qRound(params["threshold"].toDouble(0.25) * 100));
            form->addRow("Threshold", thr);

            soft = new QSlider(Qt::Horizontal);
            soft->setRange(0, 100);
            soft->setValue(qRound(params["smoothness"].toDouble(0.10) * 100));
            form->addRow("Smoothness", soft);
            layout->addLayout(form);
        }, [&]() {
            params["r"] = key->red();
            params["g"] = key->green();
            params["b"] = key->blue();
            params["threshold"] = thr->value() / 100.0;
            params["smoothness"] = soft->value() / 100.0;
        });
        return ok;
    };
    return d;
}

// ── Blur (decorator) ─────────────────────────────────────────────────────────
ProcessEffectDescriptor makeBlur() {
    ProcessEffectDescriptor d;
    d.id = 8;
    d.name = "Blur";
    d.menuLabel = "Blur";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"radius", 6}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        // GPU proof-of-concept: blur runs as two fragment-shader passes in an
        // offscreen FBO and stays GPU-resident (glTexture()) for the compositor.
        return std::make_unique<GpuBlurSource>(std::move(inner), p["radius"].toInt(6));
    };
    d.editLabel = "Radius";
    d.editDialog = [](QWidget *parent, QJsonObject &params, const QImage &) {
        QSlider *slider = nullptr;
        const bool ok = runEffectDialog(parent, "Blur", [&](QDialog *, QVBoxLayout *layout) {
            auto *row = new QHBoxLayout;
            slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 64);
            slider->setValue(params["radius"].toInt(6));
            auto *val = new QLabel(QString::number(slider->value()) + " px");
            QObject::connect(slider, &QSlider::valueChanged, val,
                             [val](int v) { val->setText(QString::number(v) + " px"); });
            row->addWidget(new QLabel("Radius"));
            row->addWidget(slider);
            row->addWidget(val);
            layout->addLayout(row);
        }, [&]() { params["radius"] = slider->value(); });
        return ok;
    };
    return d;
}

// ── Sharpen (decorator) ──────────────────────────────────────────────────────
ProcessEffectDescriptor makeSharpen() {
    ProcessEffectDescriptor d;
    d.id = 9;
    d.name = "Sharpen";
    d.menuLabel = "Sharpen";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"amount", 1.0}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<SharpenSource>(std::move(inner), p["amount"].toDouble(1.0));
    };
    d.editLabel = "Amount";
    d.editDialog = [](QWidget *parent, QJsonObject &params, const QImage &) {
        QSlider *slider = nullptr;
        const bool ok = runEffectDialog(parent, "Sharpen", [&](QDialog *, QVBoxLayout *layout) {
            auto *row = new QHBoxLayout;
            slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 300);   // 0.00 – 3.00
            slider->setValue(qRound(params["amount"].toDouble(1.0) * 100));
            auto *val = new QLabel(QString::number(slider->value() / 100.0, 'f', 2));
            QObject::connect(slider, &QSlider::valueChanged, val,
                             [val](int v) { val->setText(QString::number(v / 100.0, 'f', 2)); });
            row->addWidget(new QLabel("Amount"));
            row->addWidget(slider);
            row->addWidget(val);
            layout->addLayout(row);
        }, [&]() { params["amount"] = slider->value() / 100.0; });
        return ok;
    };
    return d;
}

// ── Keystone / Perspective (decorator) ───────────────────────────────────────
ProcessEffectDescriptor makeKeystone() {
    ProcessEffectDescriptor d;
    d.id = 10;
    d.name = "Keystone";
    d.menuLabel = "Keystone / Perspective";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{
        {"corners", QJsonArray{QJsonArray{0.0, 0.0}, QJsonArray{1.0, 0.0},
                               QJsonArray{1.0, 1.0}, QJsonArray{0.0, 1.0}}}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<KeystoneSource>(std::move(inner), cornersFromParams(p));
    };
    d.editLabel = "Edit Corners";
    d.editDialog = [](QWidget *parent, QJsonObject &params, const QImage &referenceFrame) {
        PerspectiveSelectorWidget *sel = nullptr;
        const bool ok = runEffectDialog(parent, "Keystone / Perspective",
                                        [&](QDialog *dlg, QVBoxLayout *layout) {
            sel = new PerspectiveSelectorWidget(dlg);
            if (!referenceFrame.isNull()) sel->setFrame(referenceFrame);
            sel->setCorners(cornersFromParams(params));
            layout->addWidget(sel);
            auto *reset = new QPushButton("Reset Corners");
            QObject::connect(reset, &QPushButton::clicked, sel,
                             [sel] { sel->resetCorners(); });
            layout->addWidget(reset);
        }, [&]() { params["corners"] = pointsToArray(sel->corners()); });
        return ok;
    };
    return d;
}

// ── Polygonal masking (decorator) ────────────────────────────────────────────
ProcessEffectDescriptor makePolygonMask() {
    ProcessEffectDescriptor d;
    d.id = 11;
    d.name = "Poly Mask";
    d.menuLabel = "Polygonal Masking";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"points", QJsonArray{}}, {"invert", false}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<PolygonMaskSource>(std::move(inner),
                                                   pointsFromArray(p["points"].toArray()),
                                                   p["invert"].toBool(false));
    };
    d.editLabel = "Edit Mask";
    d.editDialog = [](QWidget *parent, QJsonObject &params, const QImage &referenceFrame) {
        PolygonSelectorWidget *sel = nullptr;
        QCheckBox *invert = nullptr;
        const bool ok = runEffectDialog(parent, "Polygonal Masking",
                                        [&](QDialog *dlg, QVBoxLayout *layout) {
            sel = new PolygonSelectorWidget(dlg);
            if (!referenceFrame.isNull()) sel->setFrame(referenceFrame);
            sel->setPoints(pointsFromArray(params["points"].toArray()));
            layout->addWidget(sel);
            auto *row = new QHBoxLayout;
            invert = new QCheckBox("Mask outside (invert)");
            invert->setChecked(params["invert"].toBool(false));
            auto *clear = new QPushButton("Clear Points");
            QObject::connect(clear, &QPushButton::clicked, sel,
                             [sel] { sel->clearPoints(); });
            row->addWidget(invert);
            row->addStretch();
            row->addWidget(clear);
            layout->addLayout(row);
        }, [&]() {
            params["points"] = pointsToArray(sel->points());
            params["invert"] = invert->isChecked();
        });
        return ok;
    };
    return d;
}

} // namespace

namespace ProcessEffects {

const QVector<ProcessEffectDescriptor> &all() {
    static const QVector<ProcessEffectDescriptor> registry{
        makeCrop(), makeFlipH(), makeFlipV(), makeSegment(),
        makeFlip(), makeRotate(), makeOpacity(), makeChromaKey(),
        makeBlur(), makeSharpen(), makeKeystone(), makePolygonMask()};
    return registry;
}

const ProcessEffectDescriptor *byId(int id) {
    for (const ProcessEffectDescriptor &d : all())
        if (d.id == id) return &d;
    return nullptr;
}

static QString effectSlug(const QString &s) {
    QString out;
    for (QChar c : s.trimmed().toLower()) {
        if (c.isLetterOrNumber())
            out += c;
        else if (!out.isEmpty() && out.back() != QLatin1Char('_'))
            out += QLatin1Char('_');
    }
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    return out;
}

const ProcessEffectDescriptor *byName(const QString &name) {
    const QString key = name.trimmed();
    if (key.isEmpty())
        return nullptr;
    bool ok = false;
    const int id = key.toInt(&ok);
    if (ok)
        return byId(id);
    const QString slug = effectSlug(key);
    for (const ProcessEffectDescriptor &d : all()) {
        if (d.name.compare(key, Qt::CaseInsensitive) == 0)
            return &d;
        if (d.menuLabel.compare(key, Qt::CaseInsensitive) == 0)
            return &d;
        if (effectSlug(d.name) == slug || effectSlug(d.menuLabel) == slug)
            return &d;
    }
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
