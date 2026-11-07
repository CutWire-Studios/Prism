#include "core/effects/EffectPassRunner.h"
#include "core/effects/ShaderEffects.h"
#include "core/graph/ProcessEffects.h"
#include "core/effects/ShaderEffectSource.h"
#include "core/effects/LutLoader.h"
#include "core/sources/ImageSource.h"
#include "core/sources/MediaSource.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

namespace {
class StubSource : public MediaSource {
public:
    Type type() const override { return Type::Canvas; }
    bool isReady() const override { return false; }
    QSize frameSize() const override { return {}; }
    const uint8_t *frameData() const override { return nullptr; }
    bool nextFrame() override { return false; }
};

struct HistoryRig {
    ImageSource *img = nullptr;
    std::unique_ptr<ShaderEffectSource> src;

    bool init(const QString &slug, const QJsonObject &over) {
        const ShaderEffectSpec *spec = ShaderEffects::bySlug(slug);
        if (!spec) return false;
        QJsonObject params = spec->defaultParams();
        for (auto it = over.begin(); it != over.end(); ++it) params[it.key()] = it.value();
        auto image = std::make_unique<ImageSource>();
        img = image.get();
        src = std::make_unique<ShaderEffectSource>(std::move(image), *spec, params);
        return true;
    }

    // Feeds one frame and reads back the centre pixel as RGBA.
    bool feed(const QColor &c, QRgb &out) {
        QImage frame(64, 64, QImage::Format_RGBA8888);
        frame.fill(c);
        if (!img->setImage(frame) || !src->nextFrame()) return false;
        const uint8_t *data = src->frameData();
        if (!data) return false;
        const QImage o(data, 64, 64, 64 * 4, QImage::Format_RGBA8888);
        out = o.pixel(32, 32);
        return true;
    }
};

bool haveGL() {
    QOffscreenSurface surface;
    surface.create();
    QOpenGLContext ctx;
    const bool ok = surface.isValid() && ctx.create() && ctx.makeCurrent(&surface);
    if (ok) ctx.doneCurrent();
    return ok;
}
} // namespace

class TestShaderEffects : public QObject {
    Q_OBJECT

private slots:
    void hexColor_data() {
        QTest::addColumn<QString>("in");
        QTest::addColumn<int>("r");
        QTest::addColumn<int>("g");
        QTest::addColumn<int>("b");
        QTest::addColumn<int>("a");
        QTest::newRow("rrggbbaa") << "#11223344" << 0x11 << 0x22 << 0x33 << 0x44;
        QTest::newRow("rrggbb") << "#112233" << 0x11 << 0x22 << 0x33 << 0xff;
        QTest::newRow("named") << "red" << 0xff << 0x00 << 0x00 << 0xff;
    }

    void hexColor() {
        QFETCH(QString, in);
        QFETCH(int, r);
        QFETCH(int, g);
        QFETCH(int, b);
        QFETCH(int, a);
        const QColor c = ShaderEffects::parseHexColor(in);
        QCOMPARE(c.red(), r);
        QCOMPARE(c.green(), g);
        QCOMPARE(c.blue(), b);
        QCOMPARE(c.alpha(), a);
    }

    void paramChange_updatesDecoratorInPlace() {
        const ShaderEffectSpec *spec = nullptr;
        const ShaderParam *param = nullptr;
        for (const ShaderEffectSpec &s : ShaderEffects::all())
            for (const ShaderParam &p : s.params)
                if (!spec && p.type == ShaderParam::Float) { spec = &s; param = &p; }
        QVERIFY(spec);

        QJsonObject before = spec->defaultParams();
        QJsonObject after = before;
        after[param->name] = param->def + 0.5;

        auto inner = std::make_unique<StubSource>();
        MediaSource *innerPtr = inner.get();
        QVector<SourceEffectRef> effects{{spec->id, before}};
        std::unique_ptr<MediaSource> top =
            ProcessEffects::applySourceEffects(std::move(inner), effects);
        auto *decorator = dynamic_cast<ShaderEffectSource *>(top.get());
        QVERIFY(decorator);
        QCOMPARE(decorator->inner(), innerPtr);
        QCOMPARE(decorator->params(), before);

        const QVector<SourceEffectRef> edited{{spec->id, after}};
        QCOMPARE(ProcessEffects::sourceEffectsKey(edited), ProcessEffects::sourceEffectsKey(effects));
        QVERIFY(ProcessEffects::sourceEffectsParamsKey(edited) != ProcessEffects::sourceEffectsParamsKey(effects));

        QCOMPARE(ProcessEffects::updateSourceEffects(top.get(), edited), innerPtr);
        QCOMPARE(decorator->params(), after);
        QCOMPARE(dynamic_cast<ShaderEffectSource *>(top.get()), decorator);
    }

    void structureChange_changesKeyAndUnsupportedDecoratorsRebuild() {
        const int shaderId = ShaderEffects::all().first().id;
        const QVector<SourceEffectRef> ab{{shaderId, {}}, {4, {{"direction", 1}}}};
        const QVector<SourceEffectRef> ba{{4, {{"direction", 1}}}, {shaderId, {}}};
        QVERIFY(ProcessEffects::sourceEffectsKey(ab) != ProcessEffects::sourceEffectsKey(ba));

        std::unique_ptr<MediaSource> top =
            ProcessEffects::applySourceEffects(std::make_unique<StubSource>(), ab);
        QCOMPARE(ProcessEffects::updateSourceEffects(top.get(), ab), nullptr);
    }

    void manifest_hasFragResourceForEveryEntry() {
        QVERIFY(!ShaderEffects::all().isEmpty());
        for (const ShaderEffectSpec &s : ShaderEffects::all()) {
            QVERIFY2(QFile::exists(QStringLiteral(":/shaders/effects/") + s.fragFile),
                     qPrintable(s.slug));
            QVERIFY2(!ShaderEffects::fragmentSource(s).isEmpty(), qPrintable(s.slug));
        }
    }

    void manifest_idsAndSlugsAreUnique() {
        QSet<int> ids;
        QSet<QString> slugs;
        for (const ShaderEffectSpec &s : ShaderEffects::all()) {
            QVERIFY2(s.id >= 12, qPrintable(s.slug));
            QVERIFY2(!s.slug.isEmpty(), "empty slug");
            QVERIFY2(!ids.contains(s.id), qPrintable(s.slug));
            QVERIFY2(!slugs.contains(s.slug), qPrintable(s.slug));
            ids.insert(s.id);
            slugs.insert(s.slug);
        }
    }

    void builtinIds_keepTheirNames() {
        const QList<QPair<int, QString>> pinned = {
            {0, "Crop"}, {1, "Flip H"}, {2, "Flip V"}, {3, "Remove BG"},
            {4, "Flip"}, {5, "Rotate"}, {6, "Opacity"}, {7, "Chroma Key"},
            {8, "Blur"}, {9, "Sharpen"}, {10, "Keystone"}, {11, "Poly Mask"}};
        for (const auto &p : pinned) {
            const ProcessEffectDescriptor *d = ProcessEffects::byId(p.first);
            QVERIFY2(d, qPrintable(QString::number(p.first)));
            QCOMPARE(d->name, p.second);
        }
    }

    void descriptors_matchSpecs() {
        for (const ShaderEffectSpec &s : ShaderEffects::all()) {
            const ProcessEffectDescriptor *d = ProcessEffects::byId(s.id);
            QVERIFY2(d, qPrintable(s.slug));
            QCOMPARE(d->defaultParams, s.defaultParams());
            QCOMPARE(ProcessEffects::byName(s.slug), d);
            for (const ShaderParam &p : s.params) {
                QVERIFY(d->defaultParams.contains(p.name));
                if (p.type == ShaderParam::Float)
                    QCOMPARE(d->defaultParams.value(p.name).toDouble(), p.def);
            }
        }
        const ShaderEffectSpec *inv = ShaderEffects::bySlug("invert");
        QVERIFY(inv);
        QCOMPARE(inv->id, 12);
        QCOMPARE(inv->params.size(), 1);
        QCOMPARE(inv->defaultParams().value("amount").toDouble(), 1.0);
    }

    void assembledSources_compileAndLink() {
        QOffscreenSurface surface;
        surface.create();
        QOpenGLContext ctx;
        if (!surface.isValid() || !ctx.create() || !ctx.makeCurrent(&surface))
            QSKIP("No OpenGL context available in this environment");

        for (const ShaderEffectSpec &s : ShaderEffects::all()) {
            QOpenGLShaderProgram prog;
            const bool ok =
                prog.addShaderFromSourceCode(QOpenGLShader::Vertex,
                    "attribute vec2 position;\nvarying vec2 v_uv;\n"
                    "void main() { v_uv = position * 0.5 + 0.5; gl_Position = vec4(position, 0.0, 1.0); }\n")
                && prog.addShaderFromSourceCode(QOpenGLShader::Fragment, ShaderEffects::fragmentSource(s))
                && prog.link();
            QVERIFY2(ok, qPrintable(s.slug + ": " + prog.log()));
        }
        ctx.doneCurrent();
    }

    void cube_parsesIdentity() {
        QFile f(QStringLiteral(PRISM_TEST_DATA_DIR "/identity2.cube"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        int size = 0;
        std::vector<float> rgb;
        QVERIFY(Lut::parseCube(f.readAll(), size, rgb));
        QCOMPARE(size, 2);
        QCOMPARE(int(rgb.size()), 24);
        QCOMPARE(rgb[3], 1.f);   // second entry: red = 1
        QCOMPARE(rgb[4], 0.f);
        QCOMPARE(rgb[3 * 7 + 2], 1.f);   // last entry: blue = 1

        const QImage atlas = Lut::atlasFromLattice(size, rgb);
        QCOMPARE(atlas.size(), QSize(Lut::kAtlas, Lut::kAtlas));
        auto px = [&](int tile, int r, int g) {
            return atlas.pixelColor((tile % 8) * 64 + r, (tile / 8) * 64 + g);
        };
        QCOMPARE(px(0, 0, 0), QColor(0, 0, 0));
        QCOMPARE(px(0, 63, 0), QColor(255, 0, 0));
        QCOMPARE(px(0, 0, 63), QColor(0, 255, 0));
        QCOMPARE(px(63, 0, 0), QColor(0, 0, 255));
        QCOMPARE(px(63, 63, 63), QColor(255, 255, 255));
        QVERIFY(qAbs(px(0, 32, 0).red() - 130) <= 1);
        QVERIFY(qAbs(px(32, 0, 0).blue() - 130) <= 1);
        QCOMPARE(px(5, 10, 20).alpha(), 255);
    }

    void cube_rejectsMalformed() {
        int size = 0;
        std::vector<float> rgb;
        QVERIFY(!Lut::parseCube("LUT_1D_SIZE 4\n0 0 0\n1 1 1\n", size, rgb));
        QVERIFY(!Lut::parseCube("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n", size, rgb));
        QVERIFY(!Lut::parseCube("0 0 0\n", size, rgb));
        QVERIFY(Lut::load(QStringLiteral("/nonexistent/none.cube")).isNull());
    }

    void lut_loadsStripPngAndCube() {
        const QImage cube = Lut::load(QStringLiteral(PRISM_TEST_DATA_DIR "/identity2.cube"));
        QCOMPARE(cube.size(), QSize(Lut::kAtlas, Lut::kAtlas));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const int n = 4;
        QImage strip(n * n, n, QImage::Format_RGBA8888);
        for (int b = 0; b < n; ++b)
            for (int g = 0; g < n; ++g)
                for (int r = 0; r < n; ++r)
                    strip.setPixelColor(b * n + r, g, QColor(r * 85, g * 85, b * 85));
        const QString path = dir.filePath(QStringLiteral("strip.png"));
        QVERIFY(strip.save(path));
        const QImage atlas = Lut::load(path);
        QCOMPARE(atlas.size(), QSize(Lut::kAtlas, Lut::kAtlas));
        QCOMPARE(atlas.pixelColor(63, 0), QColor(255, 0, 0));
        QCOMPARE(atlas.pixelColor(0, 63), QColor(0, 255, 0));
        QCOMPARE(atlas.pixelColor(448, 448), QColor(0, 0, 255));
    }

    void goldenPixels_data() {
        QTest::addColumn<QJsonObject>("c");
        QFile f(QStringLiteral(PRISM_TEST_DATA_DIR "/effects_golden.json"));
        QVERIFY2(f.open(QIODevice::ReadOnly), "cannot open effects_golden.json");
        const QJsonArray cases = QJsonDocument::fromJson(f.readAll()).array();
        QVERIFY(!cases.isEmpty());
        for (int i = 0; i < cases.size(); ++i) {
            const QJsonObject o = cases.at(i).toObject();
            QTest::newRow(qPrintable(QStringLiteral("%1#%2").arg(o.value("slug").toString()).arg(i))) << o;
        }
    }

    void goldenPixels() {
        QFETCH(QJsonObject, c);
        QOffscreenSurface surface;
        surface.create();
        QOpenGLContext ctx;
        if (!surface.isValid() || !ctx.create() || !ctx.makeCurrent(&surface))
            QSKIP("No OpenGL context available in this environment");
        ctx.doneCurrent();

        const ShaderEffectSpec *spec = ShaderEffects::bySlug(c.value("slug").toString());
        QVERIFY2(spec, "unknown slug");

        const QJsonArray sz = c.value("size").toArray();
        const int w = sz.size() == 2 ? sz.at(0).toInt() : 64;
        const int h = sz.size() == 2 ? sz.at(1).toInt() : 64;

        auto makeInput = [&](const QJsonObject &in) -> std::unique_ptr<ImageSource> {
            const QString type = in.value("type").toString();
            QImage img(w, h, QImage::Format_RGBA8888);
            if (type == "solid") {
                img.fill(ShaderEffects::parseHexColor(in.value("color").toString()));
            } else if (type == "hgradient") {
                const QColor a = ShaderEffects::parseHexColor(in.value("from").toString());
                const QColor b = ShaderEffects::parseHexColor(in.value("to").toString());
                for (int x = 0; x < w; ++x) {
                    const double t = w > 1 ? double(x) / (w - 1) : 0.0;
                    const QColor px = QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                                                       a.greenF() + (b.greenF() - a.greenF()) * t,
                                                       a.blueF() + (b.blueF() - a.blueF()) * t,
                                                       a.alphaF() + (b.alphaF() - a.alphaF()) * t);
                    for (int y = 0; y < h; ++y) {
                        uchar *p = img.scanLine(y) + x * 4;
                        p[0] = uchar(px.red()); p[1] = uchar(px.green());
                        p[2] = uchar(px.blue()); p[3] = uchar(px.alpha());
                    }
                }
            } else {
                QTest::qFail(qPrintable("unknown input type " + type), __FILE__, __LINE__);
                return std::unique_ptr<ImageSource>();
            }
            auto image = std::make_unique<ImageSource>();
            if (!image->setImage(img)) {
                QTest::qFail("setImage failed", __FILE__, __LINE__);
                return std::unique_ptr<ImageSource>();
            }
            return image;
        };

        auto image = makeInput(c.value("input").toObject());
        QVERIFY(image);
        QJsonObject params = spec->defaultParams();
        const QJsonObject over = c.value("params").toObject();
        for (auto it = over.begin(); it != over.end(); ++it) {
            QJsonValue v = it.value();
            if (v.isString())
                v = v.toString().replace(QStringLiteral("$DATA"), QStringLiteral(PRISM_TEST_DATA_DIR));
            params[it.key()] = v;
        }

        ShaderEffectSource src(std::move(image), *spec, params);
        const QJsonObject extra = c.value("inputs").toObject();
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const int slot = spec->inputNames.indexOf(it.key());
            QVERIFY2(slot >= 0, qPrintable("unknown extra input " + it.key()));
            auto aux = makeInput(it.value().toObject());
            QVERIFY(aux);
            src.setAux(slot, std::move(aux));
        }
        QVERIFY(src.nextFrame());
        const uint8_t *data = src.frameData();
        QVERIFY(data);
        const QImage out(data, w, h, w * 4, QImage::Format_RGBA8888);

        const QJsonArray samples = c.value("samples").toArray();
        QVERIFY(!samples.isEmpty());
        for (const QJsonValue &sv : samples) {
            const QJsonObject s = sv.toObject();
            const QJsonArray uv = s.value("uv").toArray();
            const QJsonArray exp = s.value("rgba").toArray();
            const int tol = s.value("tol").toInt(3);
            const int x = qRound(uv.at(0).toDouble() * (w - 1));
            const int y = qRound(uv.at(1).toDouble() * (h - 1));
            const uchar *p = out.constScanLine(y) + x * 4;
            for (int ch = 0; ch < 4; ++ch)
                QVERIFY2(qAbs(int(p[ch]) - exp.at(ch).toInt()) <= tol,
                         qPrintable(QStringLiteral("uv(%1,%2) px(%3,%4) ch%5: got %6 expected %7 (got rgba %8,%9,%10,%11)")
                                        .arg(uv.at(0).toDouble()).arg(uv.at(1).toDouble()).arg(x).arg(y).arg(ch)
                                        .arg(p[ch]).arg(exp.at(ch).toInt()).arg(p[0]).arg(p[1]).arg(p[2]).arg(p[3])));
        }
    }

    void historyPlan_shrinksUnderTheCapAndFailsGracefully() {
        using R = EffectPassRunner;
        const auto small = R::planHistory({64, 64}, 32, true, R::kHistorySourceBytes);
        QVERIFY(small.ok);
        QCOMPARE(small.tile, QSize(64, 64));

        const auto hd = R::planHistory({1920, 1080}, 32, true, R::kHistorySourceBytes);
        QVERIFY(hd.ok);
        QVERIFY(hd.tile.width() < 1920);
        QVERIFY(hd.bytes <= R::kHistorySourceBytes);
        QVERIFY(hd.atlas.width() <= 4096 && hd.atlas.height() <= 4096);
        QVERIFY(hd.cols * hd.rows >= 32);

        QVERIFY(!R::planHistory({16384, 16384}, 0, true, R::kHistorySourceBytes).ok);
        QVERIFY(!R::planHistory({1920, 1080}, 32, false, 1024).ok);
    }

    void historyEffects_decayLikePowers_data() {
        QTest::addColumn<QString>("slug");
        QTest::addColumn<QJsonObject>("params");
        QTest::newRow("trails max") << "trails" << QJsonObject{{"decay", 0.5}, {"blend", 0}};
        QTest::newRow("trails add") << "trails" << QJsonObject{{"decay", 0.5}, {"blend", 1}};
        QTest::newRow("trails mix") << "trails" << QJsonObject{{"decay", 0.5}, {"blend", 2}};
        QTest::newRow("feedback") << "feedback"
            << QJsonObject{{"decay", 0.5}, {"zoom", 1.0}, {"rotation", 0.0}, {"mix", 1.0}};
    }

    void historyEffects_decayLikePowers() {
        QFETCH(QString, slug);
        QFETCH(QJsonObject, params);
        if (!haveGL()) QSKIP("No OpenGL context available in this environment");

        HistoryRig rig;
        QVERIFY(rig.init(slug, params));
        QRgb px = 0;
        QVERIFY(rig.feed(Qt::white, px));
        QVERIFY(qAbs(qRed(px) - 255) <= 2);
        for (int k = 1; k <= 3; ++k) {
            QVERIFY(rig.feed(Qt::black, px));
            const int expected = qRound(255.0 * std::pow(0.5, k));
            QVERIFY2(qAbs(qRed(px) - expected) <= 3,
                     qPrintable(QStringLiteral("k=%1 got %2 expected %3").arg(k).arg(qRed(px)).arg(expected)));
            QCOMPARE(qAlpha(px), 255);
        }
    }

    void frameDelay_outputsTheFrameFromNFramesAgo() {
        if (!haveGL()) QSKIP("No OpenGL context available in this environment");
        const int delay = 3;
        HistoryRig rig;
        QVERIFY(rig.init("frame_delay", {{"delay", delay}}));
        const int levels[] = {10, 60, 110, 160, 210, 250, 30};
        for (int t = 0; t < 7; ++t) {
            QRgb px = 0;
            QVERIFY(rig.feed(QColor(levels[t], levels[t], levels[t]), px));
            if (t < delay) {
                QCOMPARE(qAlpha(px), 0);
            } else {
                QVERIFY2(qAbs(qRed(px) - levels[t - delay]) <= 2,
                         qPrintable(QStringLiteral("t=%1 got %2 expected %3").arg(t).arg(qRed(px)).arg(levels[t - delay])));
                QCOMPARE(qAlpha(px), 255);
            }
        }
    }

    void history_resetsWhenSourceIsResetAndFreesBudget() {
        if (!haveGL()) QSKIP("No OpenGL context available in this environment");
        const qint64 base = EffectPassRunner::historyBytesInUse();
        {
            HistoryRig rig;
            QVERIFY(rig.init("frame_delay", {{"delay", 1}}));
            QRgb px = 0;
            rig.src->setOnAir(true);
            QVERIFY(rig.feed(Qt::white, px));
            QVERIFY(rig.feed(QColor(128, 128, 128), px));
            QVERIFY(qRed(px) > 250);
            QVERIFY(EffectPassRunner::historyBytesInUse() > base);

            rig.src->setOnAir(false);
            QVERIFY(rig.feed(Qt::black, px));
            QCOMPARE(qAlpha(px), 0);

            QVERIFY(rig.feed(Qt::white, px));
            QVERIFY(qRed(px) < 5 && qAlpha(px) == 255);
            rig.src->resetHistory();
            QVERIFY(rig.feed(Qt::white, px));
            QCOMPARE(qAlpha(px), 0);
        }
        QCOMPARE(EffectPassRunner::historyBytesInUse(), base);
    }

    // Used by the glslangValidator CTest: PRISM_DUMP_SHADERS=<dir> test_shadereffects dumpSources
    void dumpSources() {
        const QString dir = qEnvironmentVariable("PRISM_DUMP_SHADERS");
        if (dir.isEmpty())
            QSKIP("PRISM_DUMP_SHADERS not set");
        QVERIFY(QDir().mkpath(dir));
        for (const ShaderEffectSpec &s : ShaderEffects::all()) {
            QFile f(QDir(dir).filePath(s.slug + QStringLiteral(".frag")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(ShaderEffects::fragmentSource(s));
        }
    }
};

QTEST_MAIN(TestShaderEffects)
#include "test_shadereffects.moc"
