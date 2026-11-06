#include "app/editors/ShaderPreview.h"

#include "core/sources/ShaderSource.h"

#include <QFile>
#include <QPainter>
#include <QSize>

namespace {

constexpr int kPreviewWidth = 320;
constexpr int kPreviewHeight = 180;

struct Preset {
    const char *name;
    const char *resource;
};

const Preset kPresets[] = {
    { "Neon Kaleidoscope",       ":/shaders/neon_kaleidoscope.glsl" },
    { "Cyberpunk Horizon Grid",  ":/shaders/cyberpunk_horizon_grid.glsl" },
    { "Audio-Reactive Wave",     ":/shaders/audio_reactive_wave.glsl" },
    { "Falling Cyber Particles", ":/shaders/falling_cyber_particles.glsl" },
    { "Rotating Hypnotic Rings", ":/shaders/rotating_hypnotic_rings.glsl" },
    { "Abstract Quantum Strings", ":/shaders/abstract_quantum_strings.glsl" },
    { "Geometric Portal Matrix", ":/shaders/geometric_portal_matrix.glsl" },
    { "Neon Particle Swarm",     ":/shaders/neon_particle_swarm.glsl" },
    { "Cosmic Plasma Flares",    ":/shaders/cosmic_plasma_flares.glsl" },
    { "Audio-Pulse VU Bars",     ":/shaders/audio_pulse_vu_bars.glsl" },
    { "Radial Spectrum Ring",    ":/shaders/audio_radial_spectrum.glsl" },
    { "LED Matrix Equalizer",    ":/shaders/audio_led_matrix.glsl" },
    { "Spectrum Mirror Skyline", ":/shaders/audio_spectrum_mirror.glsl" },
    { "Spectral Bloom",          ":/shaders/audio_spectral_bloom.glsl" },
    { "Particle Terrain",        ":/shaders/audio_particle_terrain.glsl" },
    { "Neon Wave Curtain",       ":/shaders/audio_wave_curtain.glsl" },
};
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

} // namespace

ShaderPreview::ShaderPreview(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
}

ShaderPreview::~ShaderPreview() = default;

void ShaderPreview::paint(QPainter *painter)
{
    if (!m_frame.isNull())
        painter->drawImage(boundingRect(), m_frame);
}

QStringList ShaderPreview::presetNames() const
{
    QStringList names;
    for (int i = 0; i < kPresetCount; ++i)
        names.append(QString::fromUtf8(kPresets[i].name));
    return names;
}

QString ShaderPreview::presetCode(int row) const
{
    if (row < 0 || row >= kPresetCount)
        return {};
    QFile file(QString::fromUtf8(kPresets[row].resource));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

int ShaderPreview::presetIndexFor(const QString &code) const
{
    const QString trimmed = code.trimmed();
    for (int i = 0; i < kPresetCount; ++i) {
        if (presetCode(i) == trimmed)
            return i;
    }
    return -1;
}

void ShaderPreview::refresh(const QString &code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty())
        return;

    if (!m_source)
        m_source = std::make_unique<ShaderSource>(trimmed, QSize(kPreviewWidth, kPreviewHeight));
    else
        m_source->setShaderCode(trimmed);

    m_source->nextFrame();

    if (!m_source->isReady()) {
        setError(tr("Could not initialize OpenGL offscreen context."));
        return;
    }

    setError(m_source->isCompiled() ? QString() : m_source->lastError());
    m_frame = QImage(m_source->frameData(), kPreviewWidth, kPreviewHeight,
                     kPreviewWidth * 3, QImage::Format_RGB888)
                  .copy();
    update();
}

void ShaderPreview::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}
