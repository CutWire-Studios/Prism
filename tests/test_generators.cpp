#include "core/effects/Generators.h"
#include "core/graph/ClipGraph.h"
#include "core/sources/ShaderSource.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QSet>
#include <QtTest>

namespace {
bool near(const uint8_t *px, const QColor &c, int tol = 6) {
    return std::abs(px[0] - c.red()) <= tol && std::abs(px[1] - c.green()) <= tol
           && std::abs(px[2] - c.blue()) <= tol;
}
} // namespace

class TestGenerators : public QObject {
    Q_OBJECT

private slots:
    void manifest_complete() {
        QVERIFY(Generators::all().size() >= 14);
        QSet<QString> slugs;
        for (const ShaderGeneratorSpec &g : Generators::all()) {
            QVERIFY2(!slugs.contains(g.slug), qPrintable(g.slug));
            slugs.insert(g.slug);
            QVERIFY2(QFile::exists(QStringLiteral(":/shaders/generators/") + g.fragFile), qPrintable(g.slug));
            QVERIFY2(!g.description.isEmpty(), qPrintable(g.slug));
            QVERIFY2(!Generators::fragmentSource(g).isEmpty(), qPrintable(g.slug));
            QVERIFY2(!Generators::editableSource(g, {}).isEmpty(), qPrintable(g.slug));
        }
    }

    void assembled_compileAndLink() {
        QOffscreenSurface surface;
        surface.create();
        QOpenGLContext ctx;
        if (!surface.isValid() || !ctx.create() || !ctx.makeCurrent(&surface))
            QSKIP("No OpenGL context available in this environment");
        const char *vertex = "attribute vec2 position;\nvoid main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
        for (const ShaderGeneratorSpec &g : Generators::all()) {
            for (int standalone = 0; standalone < 2; ++standalone) {
                QOpenGLShaderProgram prog;
                const QByteArray src = standalone ? Generators::editableSource(g, {}) : Generators::fragmentSource(g);
                const bool ok = prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
                                && prog.addShaderFromSourceCode(QOpenGLShader::Fragment, src) && prog.link();
                QVERIFY2(ok, qPrintable(g.slug + ": " + prog.log()));
            }
        }
        ctx.doneCurrent();
    }

    void linearGradient_endsMatchColours() {
        const ShaderGeneratorSpec *spec = Generators::bySlug(QStringLiteral("gradient_linear"));
        QVERIFY(spec);
        ShaderSource src(QString(), QSize(64, 32));
        src.setPreset(*spec);
        src.updateParams({{"angle", 0}, {"speed", 0}, {"use_mid", false},
                          {"color_a", QJsonArray{1.0, 0.0, 0.0, 1.0}},
                          {"color_b", QJsonArray{0.0, 0.0, 1.0, 1.0}}});
        if (!src.nextFrame() || !src.isCompiled())
            QSKIP("No OpenGL context available in this environment");
        const uint8_t *d = src.frameData();
        QVERIFY(near(d + (16 * 64 + 0) * 3, QColor(255, 0, 0), 12));
        QVERIFY(near(d + (16 * 64 + 63) * 3, QColor(0, 0, 255), 12));
    }

    void descriptor_presetRoundTrips() {
        ClipGraph g;
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::Shader;
        desc.preset = QStringLiteral("noise_fbm");
        const auto id = g.addInputNode(desc);
        QVERIFY(id);
        QVERIFY(g.setParams(id, {{"params", QVariantMap{{"scale", 7.0}}}}));

        ClipGraph h;
        QVERIFY(h.fromJson(g.toJson()));
        QCOMPARE(h.sourceDescriptor(id).preset, QStringLiteral("noise_fbm"));
        const QJsonObject p = h.sourceParams(id);
        QCOMPARE(p.value("scale").toDouble(), 7.0);
        QVERIFY(p.contains("speed"));
    }

    void updateParams_doesNotRecompile() {
        const ShaderGeneratorSpec *spec = Generators::bySlug(QStringLiteral("noise_fbm"));
        QVERIFY(spec);
        ShaderSource src(QString(), QSize(32, 32));
        src.setPreset(*spec);
        if (!src.nextFrame() || !src.isCompiled())
            QSKIP("No OpenGL context available in this environment");
        const int compiles = src.compileCount();
        QVERIFY(src.updateParams({{"scale", 12.0}}));
        QVERIFY(src.nextFrame());
        QCOMPARE(src.compileCount(), compiles);
    }
};

QTEST_MAIN(TestGenerators)
#include "test_generators.moc"
