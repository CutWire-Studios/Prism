#include "core/effects/ShaderEffects.h"
#include "core/graph/ProcessEffects.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QSet>
#include <QtTest>

class TestShaderEffects : public QObject {
    Q_OBJECT

private slots:
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
