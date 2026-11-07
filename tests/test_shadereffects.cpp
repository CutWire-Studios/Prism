#include "core/effects/ShaderEffects.h"
#include "core/graph/ProcessEffects.h"
#include "core/effects/ShaderEffectSource.h"
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
#include <QtTest>

namespace {
class StubSource : public MediaSource {
public:
    Type type() const override { return Type::Canvas; }
    bool isReady() const override { return false; }
    QSize frameSize() const override { return {}; }
    const uint8_t *frameData() const override { return nullptr; }
    bool nextFrame() override { return false; }
};
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
        const QJsonObject in = c.value("input").toObject();
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
            QFAIL(qPrintable("unknown input type " + type));
        }

        auto image = std::make_unique<ImageSource>();
        QVERIFY(image->setImage(img));
        QJsonObject params = spec->defaultParams();
        const QJsonObject over = c.value("params").toObject();
        for (auto it = over.begin(); it != over.end(); ++it) params[it.key()] = it.value();

        ShaderEffectSource src(std::move(image), *spec, params);
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
