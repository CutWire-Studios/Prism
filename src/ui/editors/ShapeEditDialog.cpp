#include "ui/editors/ShapeEditDialog.h"

#include "core/render/SkiaRender.h"
#include "core/sources/ShapeSource.h"
#include "ui/editors/style/ShadingLayerStackEditor.h"
#include "ui/editors/style/StyleControls.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {

using prism::ShapeKind;

QPixmap composePreview(const QImage &frame, const QSize &viewport) {
    const QSize inner = viewport - QSize(12, 12);
    if (frame.isNull() || inner.width() < 16 || inner.height() < 16)
        return QPixmap();

    QSize target = frame.size();
    target.scale(inner, Qt::KeepAspectRatio);
    QPixmap pm(target);
    QPainter p(&pm);
    const int cell = 10;
    for (int y = 0; y < target.height(); y += cell)
        for (int x = 0; x < target.width(); x += cell)
            p.fillRect(x, y, cell, cell,
                       ((x / cell + y / cell) % 2) ? QColor(0x1a, 0x1a, 0x1a)
                                                   : QColor(0x24, 0x24, 0x24));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(QPoint(0, 0), target), frame);
    p.setPen(QColor(0x3a, 0x3a, 0x3a));
    p.setBrush(Qt::NoBrush);
    p.drawRect(0, 0, target.width() - 1, target.height() - 1);
    return pm;
}

QIcon thumbFor(const prism::ShapeCatalogEntry &entry) {
    constexpr QSize canvas(88, 64);
    QSizeF box(canvas.width() - 12, canvas.height() - 12);
    QSizeF fit = box;
    if (entry.aspect > 0) {
        fit = QSizeF(entry.aspect, 1.0);
        fit.scale(box, Qt::KeepAspectRatio);
    }
    const QRectF bounds(QPointF((canvas.width() - fit.width()) / 2, (canvas.height() - fit.height()) / 2), fit);
    return QIcon(QPixmap::fromImage(prism::renderShape(entry.style, canvas, bounds, 0.0)));
}

bool isOneOf(ShapeKind kind, std::initializer_list<ShapeKind> kinds) {
    for (ShapeKind k : kinds)
        if (k == kind)
            return true;
    return false;
}

} // namespace

ShapeEditDialog::ShapeEditDialog(const SourceDescriptor &initial, QWidget *parent)
    : QDialog(parent)
    , m_initial(initial)
    , m_style(ShapeSource::styleFromDescriptor(initial))
{
    setWindowTitle(tr("Shape"));
    setMinimumSize(1020, 640);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);
    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    root->addLayout(row, 1);

    auto *left = new QVBoxLayout;
    left->setSpacing(6);
    auto *shapeHeader = new QLabel(tr("SHAPE"), this);
    shapeHeader->setObjectName(QStringLiteral("sectionHeader"));
    left->addWidget(shapeHeader);
    m_gallery = new QListWidget(this);
    m_gallery->setViewMode(QListView::IconMode);
    m_gallery->setIconSize(QSize(88, 64));
    m_gallery->setGridSize(QSize(104, 96));
    m_gallery->setResizeMode(QListView::Adjust);
    m_gallery->setMovement(QListView::Static);
    m_gallery->setWordWrap(true);
    m_gallery->setFixedWidth(250);
    QList<ShapeKind> seen;
    for (const prism::ShapeCatalogEntry &entry : prism::shapeCatalog()) {
        if (seen.contains(entry.style.kind))
            continue;
        seen.append(entry.style.kind);
        auto *item = new QListWidgetItem(thumbFor(entry), entry.label, m_gallery);
        item->setData(Qt::UserRole, int(entry.style.kind));
        item->setToolTip(entry.label);
        item->setTextAlignment(Qt::AlignHCenter);
    }
    left->addWidget(m_gallery, 1);
    row->addLayout(left);

    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(320, 240);
    m_preview->setProperty("role", "panel");
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    row->addWidget(m_preview, 1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFixedWidth(380);
    auto *panel = new QWidget;
    auto *pl = new QVBoxLayout(panel);
    pl->setContentsMargins(0, 0, 8, 0);
    pl->setSpacing(8);

    auto *geoHeader = new QLabel(tr("GEOMETRY"), panel);
    geoHeader->setObjectName(QStringLiteral("sectionHeader"));
    pl->addWidget(geoHeader);

    using style::SliderSpinRow;
    m_cornerRadius = new SliderSpinRow(tr("Corner radius"), 0, 400, 1, 0, QStringLiteral("px"), panel);
    pl->addWidget(m_cornerRadius);

    QVBoxLayout *gl = nullptr;
    auto box = [&](QWidget **out) {
        *out = new QWidget(panel);
        gl = new QVBoxLayout(*out);
        gl->setContentsMargins(0, 0, 0, 0);
        gl->setSpacing(6);
        pl->addWidget(*out);
    };
    box(&m_starBox);
    m_points = new SliderSpinRow(tr("Points"), 3, 60, 1, 0, QString(), m_starBox);
    m_innerRatio = new SliderSpinRow(tr("Inner radius"), 0.05, 0.95, 0.01, 2, QString(), m_starBox);
    gl->addWidget(m_points);
    gl->addWidget(m_innerRatio);
    box(&m_arrowBox);
    m_headSize = new SliderSpinRow(tr("Head size"), 0.05, 0.9, 0.01, 2, QString(), m_arrowBox);
    gl->addWidget(m_headSize);
    box(&m_shaftBox);
    m_thickness = new SliderSpinRow(tr("Thickness"), 0.05, 1.0, 0.01, 2, QString(), m_shaftBox);
    gl->addWidget(m_thickness);
    box(&m_bubbleBox);
    m_tailX = new SliderSpinRow(tr("Tail position"), 0.08, 0.92, 0.01, 2, QString(), m_bubbleBox);
    m_tailSize = new SliderSpinRow(tr("Tail size"), 0.05, 0.5, 0.01, 2, QString(), m_bubbleBox);
    gl->addWidget(m_tailX);
    gl->addWidget(m_tailSize);

    m_layers = new style::ShadingLayerStackEditor(panel);
    pl->addWidget(m_layers);
    pl->addStretch(1);
    scroll->setWidget(panel);
    row->addWidget(scroll);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(25);
    connect(m_renderTimer, &QTimer::timeout, this, &ShapeEditDialog::updatePreview);
    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(40);
    connect(m_clockTimer, &QTimer::timeout, this, &ShapeEditDialog::updatePreview);
    m_clock.start();

    m_cornerRadius->setValue(m_style.cornerRadius);
    m_points->setValue(m_style.points);
    m_innerRatio->setValue(m_style.innerRatio);
    m_headSize->setValue(m_style.headSize);
    m_thickness->setValue(m_style.thickness);
    m_tailX->setValue(m_style.tailX);
    m_tailSize->setValue(m_style.tailSize);
    m_layers->setLayers(m_style.layers);
    syncGeometryRows();

    for (int i = 0; i < m_gallery->count(); ++i)
        if (m_gallery->item(i)->data(Qt::UserRole).toInt() == int(m_style.kind))
            m_gallery->setCurrentRow(i);

    connect(m_gallery, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            selectKind(ShapeKind(item->data(Qt::UserRole).toInt()));
    });
    connect(m_cornerRadius, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.cornerRadius = v; scheduleRender(); });
    connect(m_points, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.points = int(v); scheduleRender(); });
    connect(m_innerRatio, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.innerRatio = v; scheduleRender(); });
    connect(m_headSize, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.headSize = v; scheduleRender(); });
    connect(m_thickness, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.thickness = v; scheduleRender(); });
    connect(m_tailX, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.tailX = v; scheduleRender(); });
    connect(m_tailSize, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.tailSize = v; scheduleRender(); });
    connect(m_layers, &style::ShadingLayerStackEditor::changed, this, [this] {
        m_style.layers = m_layers->layers();
        updateClock();
        scheduleRender();
    });

    updateClock();
}

void ShapeEditDialog::selectKind(ShapeKind kind) {
    m_style.kind = kind;
    syncGeometryRows();
    scheduleRender();
}

void ShapeEditDialog::syncGeometryRows() {
    const ShapeKind k = m_style.kind;
    m_cornerRadius->setVisible(k != ShapeKind::Ellipse);
    m_starBox->setVisible(isOneOf(k, {ShapeKind::Star, ShapeKind::Burst}));
    m_arrowBox->setVisible(isOneOf(k, {ShapeKind::Arrow, ShapeKind::DoubleArrow, ShapeKind::BlockArrow,
                                       ShapeKind::Chevron, ShapeKind::Banner}));
    m_shaftBox->setVisible(isOneOf(k, {ShapeKind::Arrow, ShapeKind::DoubleArrow, ShapeKind::Chevron,
                                       ShapeKind::Cross, ShapeKind::CurvedArrow}));
    m_bubbleBox->setVisible(isOneOf(k, {ShapeKind::SpeechBubble, ShapeKind::SpeechBubbleRect,
                                        ShapeKind::ThoughtBubble, ShapeKind::Callout}));
}

void ShapeEditDialog::updateClock() {
    const bool driven = m_style.isAnimated() || prism::shapeTimeDrivenPaint(m_style);
    if (driven && !m_clockTimer->isActive())
        m_clockTimer->start();
    else if (!driven)
        m_clockTimer->stop();
}

void ShapeEditDialog::scheduleRender() {
    if (!m_renderTimer->isActive())
        m_renderTimer->start();
}

void ShapeEditDialog::updatePreview() {
    const double t = m_clock.elapsed() / 1000.0;
    m_preview->setPixmap(composePreview(ShapeSource::renderDescriptor(resultDescriptor(), t), m_preview->size()));
}

SourceDescriptor ShapeEditDialog::resultDescriptor() const {
    SourceDescriptor desc = m_initial;
    desc.kind = SourceDescriptor::Kind::Shape;
    desc.shapeStyleJson = ShapeSource::styleToJson(m_style);
    if (desc.displayName.isEmpty())
        desc.displayName = tr("Shape");
    return desc;
}

void ShapeEditDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    updatePreview();
}

void ShapeEditDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    scheduleRender();
}
