#include "ui/nodes/PedalboardDialog.h"

#include "core/audio/dsp/PedalCatalog.h"
#include "ui/common/Icons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDial>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

using namespace prism::audiofx;

namespace {

constexpr int kDialSteps = 1000;
constexpr int kSpace = 8;
constexpr int kGap = 12;
constexpr int kPad = 16;
constexpr int kDialSize = 44;
constexpr int kCompactDial = 36;
constexpr int kCellW = 64;
constexpr int kCellH = 74;
constexpr int kCompactW = 60;
constexpr int kCompactH = 66;
constexpr int kGutterW = 70;
constexpr int kModCardW = 232;
constexpr int kStepsHeight = 70;
constexpr int kFlowHeight = 20;
constexpr int kPedalFlowOffset = 13;
constexpr int kSplitFlowOffset = 1 + kSpace + kCompactH / 2 - kFlowHeight / 2;
constexpr int kEmptyFlowOffset = 18;
constexpr float kMaxLaneGain = 4.0f;

using Scale = KnobSpec::Scale;

enum PaletteRole { RoleKind = Qt::UserRole, RoleArg };

class KnobDial : public QDial {
public:
    std::function<void()> onReset;

protected:
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        if (onReset) onReset();
        else QDial::mouseDoubleClickEvent(e);
    }
};

class ClickFrame : public QFrame {
public:
    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && onClick) {
            onClick();
            e->accept();
        } else {
            QFrame::mousePressEvent(e);
        }
    }
};

void repolish(QWidget *w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}

double fromNorm(Scale scale, double lo, double hi, double t) {
    if (scale == Scale::Log && lo > 0.0)
        return lo * std::pow(hi / lo, t);
    return lo + t * (hi - lo);
}

double toNorm(Scale scale, double lo, double hi, double v) {
    v = std::clamp(v, lo, hi);
    if (hi <= lo) return 0.0;
    if (scale == Scale::Log && lo > 0.0)
        return std::log(v / lo) / std::log(hi / lo);
    return (v - lo) / (hi - lo);
}

QString formatValue(double v, const QString &unit) {
    const double a = std::fabs(v);
    const int prec = a >= 100.0 ? 0 : a >= 10.0 ? 1 : 2;
    QString s = QString::number(v, 'f', prec);
    if (!unit.isEmpty())
        s += (unit.startsWith(':') ? QString() : QStringLiteral(" ")) + unit;
    return s;
}

QToolButton *flatIconButton(const char *icon, const QString &tip) {
    auto *b = new QToolButton;
    b->setObjectName(QStringLiteral("flatTool"));
    b->setToolTip(tip);
    b->setIcon(Icons::icon(icon, 12));
    b->setIconSize(QSize(12, 12));
    b->setFixedSize(20, 20);
    return b;
}

QLabel *sectionHeader(const QString &text) {
    auto *l = new QLabel(text.toUpper());
    l->setObjectName(QStringLiteral("sectionHeader"));
    return l;
}

QFrame *makeStrip(const QString &category) {
    auto *strip = new QFrame;
    strip->setObjectName(QStringLiteral("cardStrip"));
    strip->setProperty("category", category);
    strip->setFixedHeight(3);
    return strip;
}

QFrame *makeCard(const char *name) {
    auto *f = new QFrame;
    f->setObjectName(QLatin1String(name));
    auto *l = new QVBoxLayout(f);
    l->setContentsMargins(kGap, 0, kGap, kGap);
    l->setSpacing(kSpace);
    return f;
}

QLabel *cardTitle(const QString &text) {
    auto *t = new QLabel(text);
    t->setObjectName(QStringLiteral("cardTitle"));
    return t;
}

QWidget *flowItem(QWidget *w, int offset) {
    auto *wrap = new QWidget;
    auto *l = new QVBoxLayout(wrap);
    l->setContentsMargins(0, offset, 0, 0);
    l->setSpacing(0);
    l->addWidget(w);
    return wrap;
}

QWidget *flowArrow(int offset) {
    auto *a = new QLabel(QStringLiteral("▸"));
    a->setObjectName(QStringLiteral("flowArrow"));
    a->setAlignment(Qt::AlignCenter);
    a->setFixedSize(16, kFlowHeight);
    return flowItem(a, offset);
}

QWidget *flowPill(const QString &text, int offset) {
    auto *p = new QLabel(text);
    p->setObjectName(QStringLiteral("ioPill"));
    p->setAlignment(Qt::AlignCenter);
    p->setFixedHeight(kFlowHeight);
    return flowItem(p, offset);
}

int flowOffset(const RackItem &item) { return item.isSplit() ? kSplitFlowOffset : kPedalFlowOffset; }

int knobColumns(int continuous) { return continuous <= 4 ? 2 : 3; }

QString specLabel(const PedalSpec *spec, const QString &fallback) {
    return spec ? QString::fromUtf8(spec->label) : fallback;
}

void collectNodes(const std::vector<RackItem> &items, QList<QPair<QString, QString>> &out) {
    for (const RackItem &item : items) {
        if (const RackPedal *p = item.pedal()) {
            const PedalSpec *spec = pedalSpec(p->type.toStdString());
            out.append({p->id, specLabel(spec, p->type) + QStringLiteral(" (") + p->id + ')'});
        } else if (const RackSplit *s = item.split()) {
            out.append({s->id, QStringLiteral("Split (") + s->id + ')'});
            for (const RackLane &lane : s->lanes)
                collectNodes(lane.items, out);
        }
    }
}

} // namespace

struct PedalboardDialog::KnobArgs {
    QString label;
    double min = 0.0;
    double max = 1.0;
    double def = 0.0;
    Scale scale = Scale::Linear;
    QString unit;
    QStringList options;
    double value = 0.0;
    bool modulated = false;
    std::function<void(float)> set;
    std::function<void(const QPoint &)> context;
};

PedalboardDialog::KnobArgs PedalboardDialog::specArgs(const KnobSpec &k, double value) {
    KnobArgs a;
    a.label = QString::fromUtf8(k.label);
    a.min = k.min;
    a.max = k.max;
    a.def = k.defaultValue;
    a.scale = k.scale;
    a.unit = QString::fromUtf8(k.unit);
    for (const char *o : k.options) a.options << QString::fromUtf8(o);
    a.value = value;
    return a;
}

PedalboardDialog::PedalboardDialog(const QJsonObject &params, std::function<void(const QJsonObject &)> onLiveChange,
                                   QWidget *parent)
    : QDialog(parent), m_rack(fromJson(params)), m_onLive(std::move(onLiveChange)) {
    setWindowTitle(tr("Audio FX"));
    setModal(true);
    resize(1100, 640);
    setMinimumSize(800, 480);

    auto *search = new QLineEdit;
    search->setPlaceholderText(tr("Search effects"));
    search->setClearButtonEnabled(true);

    auto *palette = new QTreeWidget;
    palette->setHeaderHidden(true);
    palette->setIndentation(12);

    std::map<std::string, QTreeWidgetItem *> groups;
    auto groupFor = [&](const std::string &cat, const QString &title) {
        auto it = groups.find(cat);
        if (it != groups.end()) return it->second;
        auto *g = new QTreeWidgetItem(palette, QStringList(title));
        g->setFlags(Qt::ItemIsEnabled);
        groups[cat] = g;
        return g;
    };
    for (const PedalSpec &spec : pedalSpecs()) {
        QString title = QString::fromUtf8(spec.category);
        title[0] = title[0].toUpper();
        auto *leaf = new QTreeWidgetItem(groupFor(spec.category, title), QStringList(QString::fromUtf8(spec.label)));
        leaf->setData(0, RoleKind, QStringLiteral("pedal"));
        leaf->setData(0, RoleArg, QString::fromUtf8(spec.type));
    }
    auto *structure = groupFor("\x01structure", tr("Split"));
    auto *par = new QTreeWidgetItem(structure, QStringList(tr("Split (parallel)")));
    par->setData(0, RoleKind, QStringLiteral("parallel"));
    auto *bands = new QTreeWidgetItem(structure, QStringList(tr("Split (bands)")));
    bands->setData(0, RoleKind, QStringLiteral("bands"));
    auto *mods = groupFor("\x02mod", tr("Modulators"));
    for (const PedalSpec &spec : modulatorSpecs()) {
        auto *leaf = new QTreeWidgetItem(mods, QStringList(QString::fromUtf8(spec.label)));
        leaf->setData(0, RoleKind, QStringLiteral("mod"));
        leaf->setData(0, RoleArg, QString::fromUtf8(spec.type));
    }
    palette->expandAll();
    connect(palette, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) { addFromPalette(item); });
    connect(search, &QLineEdit::textChanged, palette, [palette](const QString &text) {
        for (int i = 0; i < palette->topLevelItemCount(); ++i) {
            QTreeWidgetItem *g = palette->topLevelItem(i);
            const bool groupHit = g->text(0).contains(text, Qt::CaseInsensitive);
            bool any = false;
            for (int j = 0; j < g->childCount(); ++j) {
                QTreeWidgetItem *c = g->child(j);
                const bool hit = text.isEmpty() || groupHit || c->text(0).contains(text, Qt::CaseInsensitive);
                c->setHidden(!hit);
                any = any || hit;
            }
            g->setHidden(!any);
            if (any) g->setExpanded(true);
        }
    });

    auto *left = new QVBoxLayout;
    left->setSpacing(kSpace);
    left->addWidget(search);
    left->addWidget(palette, 1);
    auto *leftWrap = new QWidget;
    leftWrap->setFixedWidth(220);
    left->setContentsMargins(0, 0, 0, 0);
    leftWrap->setLayout(left);

    m_boardScroll = new QScrollArea;
    m_boardScroll->setWidgetResizable(true);
    m_modScroll = new QScrollArea;
    m_modScroll->setWidgetResizable(true);
    m_modScroll->setMinimumHeight(200);
    m_modHint = new QLabel(tr("No modulators. Add one from the palette, then right-click a knob to modulate it."));
    m_modHint->setProperty("role", "secondary");

    m_status = new QLabel;
    m_status->setProperty("role", "secondary");

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(buttons);

    m_right = new QVBoxLayout;
    m_right->setSpacing(kSpace);
    m_right->addWidget(sectionHeader(tr("Signal chain")));
    m_right->addWidget(m_boardScroll, 3);
    m_right->addWidget(sectionHeader(tr("Modulators")));
    m_right->addWidget(m_modHint);
    m_right->addWidget(m_modScroll, 2);

    auto *top = new QHBoxLayout;
    top->setSpacing(kPad);
    top->addWidget(leftWrap);
    top->addLayout(m_right, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPad, kPad, kPad, kPad);
    layout->setSpacing(kGap);
    layout->addLayout(top, 1);
    layout->addLayout(bottom);

    rebuild();
}

void PedalboardDialog::edited() {
    if (m_onLive) m_onLive(toJson(m_rack));
}

void PedalboardDialog::structural() {
    edited();
    scheduleRebuild();
}

void PedalboardDialog::scheduleRebuild() {
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildPending = false;
        rebuild();
    });
}

void PedalboardDialog::showStatus(const QString &text) {
    m_status->setText(text);
}

bool PedalboardDialog::applyOp(bool ok) {
    if (!ok) {
        showStatus(tr("Not possible: limit reached (4 lanes per split, 2 levels of nesting, 64 nodes)."));
        return false;
    }
    showStatus(QString());
    structural();
    return true;
}

void PedalboardDialog::selectLane(const QString &split, int lane) {
    if (m_selSplit == split && (split.isEmpty() || m_selLane == lane)) return;
    m_selSplit = split;
    m_selLane = lane;
    scheduleRebuild();
}

RackSlot PedalboardDialog::targetSlot() const {
    RackSlot slot;
    if (const RackSplit *s = findSplit(m_rack, m_selSplit); s && m_selLane >= 0 && m_selLane < int(s->lanes.size())) {
        slot.split = m_selSplit;
        slot.lane = m_selLane;
    }
    return slot;
}

void PedalboardDialog::addFromPalette(QTreeWidgetItem *item) {
    const QString kind = item->data(0, RoleKind).toString();
    if (kind.isEmpty()) return;
    const QString arg = item->data(0, RoleArg).toString();
    const RackSlot slot = targetSlot();
    if (kind == QLatin1String("pedal")) {
        QString id;
        QString ir;
        if (arg == QLatin1String("convolution")) {
            ir = QFileDialog::getOpenFileName(this, tr("Select impulse response"), QString(),
                                              tr("WAV files (*.wav);;All files (*)"));
            if (ir.isEmpty()) return;
        }
        if (!addPedal(m_rack, arg, slot, &id)) {
            applyOp(false);
            return;
        }
        if (!ir.isEmpty())
            if (RackPedal *p = findPedal(m_rack, id)) p->irPath = ir;
        applyOp(true);
    } else if (kind == QLatin1String("mod")) {
        applyOp(addModulator(m_rack, arg));
    } else {
        applyOp(addSplit(m_rack, kind == QLatin1String("bands") ? RackSplit::Mode::Bands : RackSplit::Mode::Parallel,
                         2, slot));
    }
}

void PedalboardDialog::rebuild() {
    if (!findSplit(m_rack, m_selSplit)) m_selSplit.clear();

    const QPoint boardPos(m_boardScroll->horizontalScrollBar()->value(), m_boardScroll->verticalScrollBar()->value());
    const QPoint modPos(m_modScroll->horizontalScrollBar()->value(), m_modScroll->verticalScrollBar()->value());

    auto *board = new ClickFrame;
    board->setObjectName(QStringLiteral("board"));
    board->setProperty("target", m_selSplit.isEmpty());
    board->onClick = [this] { selectLane(QString(), 0); };
    auto *bl = new QVBoxLayout(board);
    bl->setContentsMargins(kGap, kGap, kGap, kGap);
    bl->addWidget(buildChain(m_rack.items, QString(), 0, true), 0, Qt::AlignTop | Qt::AlignLeft);
    bl->addStretch();
    m_boardScroll->setWidget(board);

    const bool hasMods = !m_rack.modulators.isEmpty();
    m_modHint->setVisible(!hasMods);
    m_modScroll->setVisible(hasMods);
    m_right->setStretchFactor(m_modScroll, hasMods ? 2 : 0);
    if (hasMods) m_modScroll->setWidget(buildModulators());

    QTimer::singleShot(0, this, [this, boardPos, modPos] {
        m_boardScroll->horizontalScrollBar()->setValue(boardPos.x());
        m_boardScroll->verticalScrollBar()->setValue(boardPos.y());
        m_modScroll->horizontalScrollBar()->setValue(modPos.x());
        m_modScroll->verticalScrollBar()->setValue(modPos.y());
    });
}

QWidget *PedalboardDialog::buildChain(const std::vector<RackItem> &items, const QString &split, int lane, bool top) {
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(kSpace);
    const int count = int(items.size());
    const int firstOff = count ? flowOffset(items.front()) : kEmptyFlowOffset;
    const int lastOff = count ? flowOffset(items.back()) : kEmptyFlowOffset;

    if (top) l->addWidget(flowPill(tr("IN"), firstOff), 0, Qt::AlignTop);
    for (int i = 0; i < count; ++i) {
        const RackItem &item = items[size_t(i)];
        l->addWidget(flowArrow(flowOffset(item)), 0, Qt::AlignTop);
        if (const RackPedal *p = item.pedal())
            l->addWidget(buildPedalCard(*p, split, lane, i, count), 0, Qt::AlignTop);
        else if (const RackSplit *s = item.split())
            l->addWidget(buildSplitCard(*s, split, lane, i, count), 0, Qt::AlignTop);
    }
    if (items.empty()) {
        l->addWidget(flowArrow(kEmptyFlowOffset), 0, Qt::AlignTop);
        auto *hint = new QLabel(tr("Click a palette item to add"));
        hint->setObjectName(QStringLiteral("dropHint"));
        hint->setAlignment(Qt::AlignCenter);
        hint->setFixedSize(180, 56);
        l->addWidget(hint, 0, Qt::AlignTop);
    }
    if (top) {
        l->addWidget(flowArrow(lastOff), 0, Qt::AlignTop);
        l->addWidget(flowPill(tr("OUT"), lastOff), 0, Qt::AlignTop);
    }
    return w;
}

QToolButton *PedalboardDialog::makeMenuButton(const std::function<void(QMenu *)> &fill) {
    auto *b = new QToolButton;
    b->setObjectName(QStringLiteral("cardMenu"));
    b->setText(QStringLiteral("⋯"));
    b->setToolTip(tr("More"));
    b->setFixedSize(22, 22);
    b->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(b);
    fill(menu);
    b->setMenu(menu);
    return b;
}

QToolButton *PedalboardDialog::makeItemMenu(const QString &id, const QString &split, int lane, int index, int count,
                                            const std::function<void(QMenu *)> &extra) {
    return makeMenuButton([&](QMenu *menu) {
        if (extra) {
            extra(menu);
            menu->addSeparator();
        }
        QAction *left = menu->addAction(tr("Move left"), this, [this, id, split, lane, index] {
            applyOp(moveItem(m_rack, id, RackSlot{split, lane, index - 1}));
        });
        left->setEnabled(index > 0);
        QAction *right = menu->addAction(tr("Move right"), this, [this, id, split, lane, index] {
            applyOp(moveItem(m_rack, id, RackSlot{split, lane, index + 2}));
        });
        right->setEnabled(index < count - 1);
        menu->addSeparator();
        menu->addAction(Icons::icon(Icons::Names::Close, 14), tr("Remove"), this,
                        [this, id] { applyOp(removeItem(m_rack, id)); });
    });
}

QWidget *PedalboardDialog::knobPanel(const std::vector<KnobArgs> &knobs) {
    auto *panel = new QWidget;
    auto *pl = new QVBoxLayout(panel);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(kSpace);

    int continuous = 0;
    for (const KnobArgs &k : knobs)
        if (k.scale != Scale::Toggle && k.scale != Scale::Choice) ++continuous;
    const int cols = knobColumns(continuous);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(kSpace);
    auto *options = new QVBoxLayout;
    options->setContentsMargins(0, 0, 0, 0);
    options->setSpacing(4);

    int i = 0;
    for (const KnobArgs &k : knobs) {
        if (k.scale == Scale::Toggle || k.scale == Scale::Choice) {
            options->addWidget(makeKnob(k));
        } else {
            grid->addWidget(makeKnob(k), i / cols, i % cols);
            ++i;
        }
    }
    if (i > 0) pl->addLayout(grid);
    if (options->count() > 0) pl->addLayout(options);
    panel->setMinimumWidth(knobColumns(continuous) * kCellW + (cols - 1) * 4);
    return panel;
}

QWidget *PedalboardDialog::buildPedalCard(const RackPedal &pedal, const QString &split, int lane, int index,
                                          int count) {
    const PedalSpec *spec = pedalSpec(pedal.type.toStdString());
    const QString id = pedal.id;

    auto *card = makeCard("pedalCard");
    card->setProperty("bypassed", pedal.bypass);
    auto *cl = qobject_cast<QVBoxLayout *>(card->layout());
    cl->addWidget(makeStrip(spec ? QString::fromUtf8(spec->category) : QString()));

    auto *head = new QHBoxLayout;
    head->setSpacing(6);
    auto *power = new QToolButton;
    power->setObjectName(QStringLiteral("pedalPower"));
    power->setToolTip(tr("Enable / bypass"));
    power->setCheckable(true);
    power->setChecked(!pedal.bypass);
    power->setFixedSize(14, 14);
    head->addWidget(power);
    head->addWidget(cardTitle(specLabel(spec, pedal.type)), 1);

    QLabel *irName = nullptr;
    const bool conv = pedal.type == QLatin1String("convolution");
    if (conv) {
        irName = new QLabel;
        irName->setObjectName(QStringLiteral("irName"));
        const QString file = pedal.irPath.isEmpty() ? tr("No IR") : QFileInfo(pedal.irPath).fileName();
        irName->setText(irName->fontMetrics().elidedText(file, Qt::ElideMiddle, 2 * kCellW));
        irName->setToolTip(pedal.irPath);
    }
    head->addWidget(makeItemMenu(id, split, lane, index, count, [&](QMenu *menu) {
        if (!conv) return;
        menu->addAction(tr("Change IR…"), this, [this, id, irName] {
            const RackPedal *p = findPedal(m_rack, id);
            if (!p) return;
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Select impulse response"), p->irPath, tr("WAV files (*.wav);;All files (*)"));
            if (path.isEmpty()) return;
            findPedal(m_rack, id)->irPath = path;
            irName->setText(irName->fontMetrics().elidedText(QFileInfo(path).fileName(), Qt::ElideMiddle, 2 * kCellW));
            irName->setToolTip(path);
            structural();
        });
    }));
    cl->addLayout(head);

    auto *body = new QWidget;
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(kSpace);

    if (irName) bl->addWidget(irName);

    if (spec) {
        std::vector<KnobArgs> knobs;
        for (const KnobSpec &k : spec->knobs) {
            const QString knobId = QString::fromUtf8(k.id);
            KnobArgs a = specArgs(k, pedal.knobs.value(knobId, k.defaultValue));
            for (const RackRoute &r : m_rack.routes)
                if (r.to == id && r.knob == knobId) a.modulated = true;
            a.set = [this, id, knobId](float v) {
                if (RackPedal *p = findPedal(m_rack, id)) {
                    p->knobs[knobId] = v;
                    edited();
                }
            };
            if (k.continuous())
                a.context = [this, id, knobId](const QPoint &pos) { routeMenu(pos, id, knobId); };
            knobs.push_back(std::move(a));
        }
        bl->addWidget(knobPanel(knobs));
    }
    body->setEnabled(!pedal.bypass);
    cl->addWidget(body);

    connect(power, &QToolButton::toggled, this, [this, id, body, card](bool on) {
        if (RackPedal *p = findPedal(m_rack, id)) {
            p->bypass = !on;
            body->setEnabled(on);
            card->setProperty("bypassed", !on);
            repolish(card);
            edited();
        }
    });
    return card;
}

QWidget *PedalboardDialog::buildSplitCard(const RackSplit &split, const QString &parent, int lane, int index,
                                          int count) {
    const QString id = split.id;
    const bool bands = split.mode == RackSplit::Mode::Bands;

    auto *card = makeCard("splitCard");
    auto *cl = qobject_cast<QVBoxLayout *>(card->layout());
    cl->setContentsMargins(kGap, kSpace, kGap, kGap);

    auto *head = new QHBoxLayout;
    head->setSpacing(kGap);
    head->addWidget(cardTitle(bands ? tr("Band split") : tr("Parallel split")), 0, Qt::AlignVCenter);
    if (!bands) {
        KnobArgs a;
        a.label = tr("Blend");
        a.min = 0;
        a.max = 1;
        a.def = 0.5;
        a.value = split.blend;
        a.set = [this, id](float v) {
            if (RackSplit *s = findSplit(m_rack, id)) {
                s->blend = v;
                edited();
            }
        };
        head->addWidget(makeKnob(a, true), 0, Qt::AlignVCenter);
        auto *xf = new QCheckBox(tr("Crossfade"));
        xf->setChecked(split.crossfade);
        connect(xf, &QCheckBox::toggled, this, [this, id](bool on) {
            if (RackSplit *s = findSplit(m_rack, id)) {
                s->crossfade = on;
                edited();
            }
        });
        head->addWidget(xf, 0, Qt::AlignVCenter);
    } else {
        const int n = int(split.crossovers.size());
        for (int i = 0; i < n; ++i) {
            KnobArgs a;
            a.label = n == 1 ? tr("Low/High") : n == 2 ? (i == 0 ? tr("Low/Mid") : tr("Mid/High")) : QStringLiteral("X%1").arg(i + 1);
            a.min = 20;
            a.max = 20000;
            a.def = 1000;
            a.scale = Scale::Log;
            a.unit = QStringLiteral("Hz");
            a.value = split.crossovers[i];
            a.set = [this, id, i](float v) {
                if (RackSplit *s = findSplit(m_rack, id); s && i < s->crossovers.size()) {
                    s->crossovers[i] = v;
                    std::sort(s->crossovers.begin(), s->crossovers.end());
                    edited();
                }
            };
            head->addWidget(makeKnob(a, true), 0, Qt::AlignVCenter);
        }
    }
    head->addStretch();
    auto *addLaneBtn = new QToolButton;
    addLaneBtn->setObjectName(QStringLiteral("addLane"));
    addLaneBtn->setText(tr("Lane"));
    addLaneBtn->setIcon(Icons::icon(Icons::Names::Add, 14));
    addLaneBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addLaneBtn->setToolTip(tr("Add a lane"));
    connect(addLaneBtn, &QToolButton::clicked, this, [this, id] { applyOp(addLane(m_rack, id)); });
    head->addWidget(addLaneBtn, 0, Qt::AlignTop);
    head->addWidget(makeItemMenu(id, parent, lane, index, count), 0, Qt::AlignTop);
    cl->addLayout(head);

    for (int li = 0; li < int(split.lanes.size()); ++li) {
        const RackLane &ln = split.lanes[size_t(li)];
        auto *row = new ClickFrame;
        row->setObjectName(QStringLiteral("laneRow"));
        row->setProperty("target", m_selSplit == id && m_selLane == li);
        row->setToolTip(QString());
        row->onClick = [this, id, li] { selectLane(id, li); };
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(kSpace, kSpace, kSpace, kSpace);
        rl->setSpacing(kSpace);

        auto *gutter = new QWidget;
        gutter->setFixedWidth(kGutterW);
        auto *gl = new QVBoxLayout(gutter);
        gl->setContentsMargins(0, 0, 0, 0);
        gl->setSpacing(2);
        auto *gh = new QHBoxLayout;
        gh->setContentsMargins(0, 0, 0, 0);
        gh->setSpacing(2);
        auto *laneTitle = new QLabel(tr("Lane %1").arg(li + 1));
        laneTitle->setObjectName(QStringLiteral("laneTitle"));
        gh->addWidget(laneTitle, 1);
        if (split.lanes.size() > 2) {
            auto *rm = flatIconButton(Icons::Names::Close, tr("Remove lane"));
            connect(rm, &QToolButton::clicked, this, [this, id, li] {
                if (m_selSplit == id) m_selSplit.clear();
                applyOp(removeLane(m_rack, id, li));
            });
            gh->addWidget(rm);
        }
        gl->addLayout(gh);
        KnobArgs g;
        g.label = tr("Gain");
        g.min = 0;
        g.max = kMaxLaneGain;
        g.def = 1;
        g.unit = QStringLiteral("x");
        g.value = ln.gain;
        g.set = [this, id, li](float v) {
            if (RackSplit *s = findSplit(m_rack, id); s && li < int(s->lanes.size())) {
                s->lanes[size_t(li)].gain = v;
                edited();
            }
        };
        gl->addWidget(makeKnob(g, true), 0, Qt::AlignHCenter);
        gl->addStretch();
        rl->addWidget(gutter, 0, Qt::AlignTop);
        rl->addWidget(buildChain(ln.items, id, li, false), 0, Qt::AlignTop);
        rl->addStretch(1);
        cl->addWidget(row);
    }
    return card;
}

QWidget *PedalboardDialog::buildTargets(const QString &modId) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    auto *title = new QLabel(tr("Targets"));
    title->setObjectName(QStringLiteral("subHeader"));
    l->addWidget(title);

    bool any = false;
    for (const RackRoute &r : m_rack.routes) {
        if (r.from != modId) continue;
        any = true;
        const QString routeId = r.id;
        QString pedalName = r.to;
        QString knob = r.knob;
        if (const RackPedal *p = findPedal(m_rack, r.to)) {
            const PedalSpec *spec = pedalSpec(p->type.toStdString());
            pedalName = specLabel(spec, p->type);
            if (spec)
                if (const KnobSpec *k = findKnob(*spec, r.knob.toStdString())) knob = QString::fromUtf8(k->label);
        }

        auto *row = new QWidget;
        auto *rl = new QVBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(0);
        auto *top = new QHBoxLayout;
        top->setSpacing(4);
        auto *name = new QLabel(QStringLiteral("%1 · %2").arg(pedalName, knob));
        name->setToolTip(r.to);
        top->addWidget(name, 1);
        auto *rm = flatIconButton(Icons::Names::Close, tr("Remove route"));
        connect(rm, &QToolButton::clicked, this, [this, routeId] { applyOp(removeRoute(m_rack, routeId)); });
        top->addWidget(rm);
        rl->addLayout(top);

        auto *bottom = new QHBoxLayout;
        bottom->setSpacing(kSpace);
        auto *depth = new QSlider(Qt::Horizontal);
        depth->setRange(-100, 100);
        depth->setValue(int(std::lround(double(r.depth) * 100)));
        auto *depthLabel = new QLabel(QString::number(depth->value()) + '%');
        depthLabel->setObjectName(QStringLiteral("knobValue"));
        depthLabel->setFixedWidth(40);
        depthLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(depth, &QSlider::valueChanged, this, [this, routeId, depthLabel](int v) {
            depthLabel->setText(QString::number(v) + '%');
            for (RackRoute &rt : m_rack.routes)
                if (rt.id == routeId) {
                    rt.depth = float(v) / 100.0f;
                    edited();
                    return;
                }
        });
        bottom->addWidget(depth, 1);
        bottom->addWidget(depthLabel);
        rl->addLayout(bottom);
        l->addWidget(row);
    }
    if (!any) {
        auto *hint = new QLabel(tr("Right-click a knob to target it"));
        hint->setProperty("role", "secondary");
        l->addWidget(hint);
    }
    return w;
}

QWidget *PedalboardDialog::buildModulators() {
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(kGap);
    QList<QPair<QString, QString>> sources;
    sources.append({QStringLiteral("input"), tr("Input")});
    collectNodes(m_rack.items, sources);

    for (const RackModulator &mod : m_rack.modulators) {
        const PedalSpec *spec = modulatorSpec(mod.type.toStdString());
        const QString id = mod.id;
        auto *card = makeCard("modCard");
        card->setMinimumWidth(kModCardW);
        auto *cl = qobject_cast<QVBoxLayout *>(card->layout());
        cl->addWidget(makeStrip(QStringLiteral("modulator")));

        auto *head = new QHBoxLayout;
        head->setSpacing(6);
        head->addWidget(cardTitle(specLabel(spec, mod.type)));
        auto *idLabel = new QLabel(id);
        idLabel->setProperty("role", "secondary");
        head->addWidget(idLabel);
        head->addStretch();
        head->addWidget(makeMenuButton([&](QMenu *menu) {
            menu->addAction(Icons::icon(Icons::Names::Close, 14), tr("Remove"), this,
                            [this, id] { applyOp(removeModulator(m_rack, id)); });
        }));
        cl->addLayout(head);

        if (spec) {
            std::vector<KnobArgs> knobs;
            for (const KnobSpec &k : spec->knobs) {
                const QString knobId = QString::fromUtf8(k.id);
                KnobArgs a = specArgs(k, mod.knobs.value(knobId, k.defaultValue));
                a.set = [this, id, knobId](float v) {
                    for (RackModulator &m : m_rack.modulators)
                        if (m.id == id) {
                            m.knobs[knobId] = v;
                            edited();
                            return;
                        }
                };
                knobs.push_back(std::move(a));
            }
            if (!knobs.empty()) cl->addWidget(knobPanel(knobs));
        }

        if (mod.type == QLatin1String("steps")) {
            auto *row = new QHBoxLayout;
            row->setSpacing(2);
            for (int i = 0; i < mod.steps.size(); ++i) {
                auto *s = new QSlider(Qt::Vertical);
                s->setRange(0, kDialSteps);
                s->setValue(int(std::lround(double(mod.steps[i]) * kDialSteps)));
                s->setFixedSize(16, kStepsHeight);
                s->setToolTip(tr("Step %1").arg(i + 1));
                connect(s, &QSlider::valueChanged, this, [this, id, i](int v) {
                    for (RackModulator &m : m_rack.modulators)
                        if (m.id == id && i < m.steps.size()) {
                            m.steps[i] = float(v) / kDialSteps;
                            edited();
                            return;
                        }
                });
                row->addWidget(s);
            }
            row->addStretch();
            cl->addLayout(row);
        }

        if (mod.type == QLatin1String("envelope")) {
            auto *row = new QHBoxLayout;
            row->setSpacing(6);
            auto *lbl = new QLabel(tr("Source"));
            lbl->setObjectName(QStringLiteral("knobLabel"));
            row->addWidget(lbl);
            auto *combo = new QComboBox;
            int cur = 0;
            for (int i = 0; i < sources.size(); ++i) {
                combo->addItem(sources[i].second, sources[i].first);
                if (sources[i].first == mod.source) cur = i;
            }
            combo->setCurrentIndex(cur);
            connect(combo, &QComboBox::currentIndexChanged, this, [this, id, combo](int) {
                for (RackModulator &m : m_rack.modulators)
                    if (m.id == id) {
                        m.source = combo->currentData().toString();
                        structural();
                        return;
                    }
            });
            row->addWidget(combo, 1);
            cl->addLayout(row);
        }

        cl->addWidget(buildTargets(id));
        l->addWidget(card, 0, Qt::AlignTop);
    }
    l->addStretch();
    return w;
}

void PedalboardDialog::routeMenu(const QPoint &globalPos, const QString &pedalId, const QString &knobId) {
    QMenu menu(this);
    auto *sub = menu.addMenu(tr("Modulate by"));
    if (m_rack.modulators.isEmpty()) {
        sub->addAction(tr("(add a modulator first)"))->setEnabled(false);
    } else {
        for (const RackModulator &m : m_rack.modulators) {
            const QString label = specLabel(modulatorSpec(m.type.toStdString()), m.type) + QStringLiteral(" (") + m.id + ')';
            const QString from = m.id;
            sub->addAction(label, this, [this, from, pedalId, knobId] {
                applyOp(addRoute(m_rack, from, pedalId, knobId));
            });
        }
    }
    for (const RackRoute &r : m_rack.routes) {
        if (r.to != pedalId || r.knob != knobId) continue;
        const QString routeId = r.id;
        menu.addAction(tr("Remove modulation (%1)").arg(r.from), this, [this, routeId] {
            applyOp(removeRoute(m_rack, routeId));
        });
    }
    menu.exec(globalPos);
}

QWidget *PedalboardDialog::makeKnob(const KnobArgs &a, bool compact) {
    const auto set = a.set;

    if (a.scale == Scale::Toggle) {
        auto *box = new QCheckBox(a.label);
        box->setChecked(a.value > 0.5);
        connect(box, &QCheckBox::toggled, box, [set](bool on) { set(on ? 1.0f : 0.0f); });
        return box;
    }
    if (a.scale == Scale::Choice) {
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(6);
        auto *lbl = new QLabel(a.label);
        lbl->setObjectName(QStringLiteral("knobLabel"));
        auto *combo = new QComboBox;
        combo->addItems(a.options);
        combo->setCurrentIndex(std::clamp(int(std::lround(a.value)), 0, std::max(0, int(a.options.size()) - 1)));
        connect(combo, &QComboBox::currentIndexChanged, combo, [set](int i) { set(float(i)); });
        rl->addWidget(lbl);
        rl->addWidget(combo, 1);
        return row;
    }

    auto *w = new QWidget;
    w->setFixedSize(compact ? kCompactW : kCellW, compact ? kCompactH : kCellH);
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(3);
    titleRow->addStretch();
    if (a.modulated) {
        auto *dot = new QLabel(QStringLiteral("●"));
        dot->setObjectName(QStringLiteral("modDot"));
        dot->setToolTip(tr("Modulated"));
        titleRow->addWidget(dot);
    }
    auto *title = new QLabel(a.label);
    title->setObjectName(QStringLiteral("knobLabel"));
    title->setFixedHeight(13);
    titleRow->addWidget(title);
    titleRow->addStretch();
    l->addLayout(titleRow);

    const bool integral = a.scale == Scale::Linear && a.max - a.min <= 64.0 && std::floor(a.min) == a.min &&
                          std::floor(a.max) == a.max && std::floor(a.def) == a.def;
    const auto valueAt = [a, integral](int dial) {
        double v = fromNorm(a.scale, a.min, a.max, double(dial) / kDialSteps);
        if (integral) v = std::round(v);
        return std::clamp(v, a.min, a.max);
    };

    const int size = compact ? kCompactDial : kDialSize;
    auto *dial = new KnobDial;
    dial->setRange(0, kDialSteps);
    dial->setFixedSize(size, size);
    dial->setValue(int(std::lround(toNorm(a.scale, a.min, a.max, a.value) * kDialSteps)));
    auto *readout = new QLabel(formatValue(a.value, a.unit));
    readout->setObjectName(QStringLiteral("knobValue"));
    readout->setAlignment(Qt::AlignHCenter);
    readout->setFixedHeight(13);

    connect(dial, &QDial::valueChanged, w, [set, valueAt, readout, unit = a.unit](int d) {
        const double v = valueAt(d);
        readout->setText(formatValue(v, unit));
        set(float(v));
    });
    QPointer<KnobDial> guard(dial);
    dial->onReset = [guard, a] {
        if (guard) guard->setValue(int(std::lround(toNorm(a.scale, a.min, a.max, a.def) * kDialSteps)));
    };
    if (a.context) {
        dial->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(dial, &QWidget::customContextMenuRequested, w, [dial, ctx = a.context](const QPoint &p) {
            ctx(dial->mapToGlobal(p));
        });
    }
    l->addWidget(dial, 0, Qt::AlignHCenter);
    l->addWidget(readout);
    return w;
}
