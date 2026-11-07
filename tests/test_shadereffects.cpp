#include "core/effects/ShaderEffects.h"
#include "core/graph/ProcessEffects.h"
#include "core/effects/ShaderEffectSource.h"
#include "core/sources/MediaSource.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
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
