#include "ui/ShaderEditDialog.h"
#include "ui_ShaderEditDialog.h"
#include "core/ShaderSource.h"
#include <QFile>
#include <QFont>
#include <QImage>

namespace {

struct Preset { const char *name; const char *resource; };

static const Preset kPresets[] = {
    { "Neon Kaleidoscope",      ":/shaders/neon_kaleidoscope.glsl"      },
    { "Cyberpunk Horizon Grid", ":/shaders/cyberpunk_horizon_grid.glsl" },
    { "Audio-Reactive Wave",    ":/shaders/audio_reactive_wave.glsl"    },
    { "Falling Cyber Particles",":/shaders/falling_cyber_particles.glsl"},
    { "Rotating Hypnotic Rings",":/shaders/rotating_hypnotic_rings.glsl"},
    { "Abstract Quantum Strings",":/shaders/abstract_quantum_strings.glsl"},
    { "Geometric Portal Matrix",":/shaders/geometric_portal_matrix.glsl"},
    { "Neon Particle Swarm",    ":/shaders/neon_particle_swarm.glsl"    },
    { "Cosmic Plasma Flares",   ":/shaders/cosmic_plasma_flares.glsl"   },
    { "Audio-Pulse VU Bars",    ":/shaders/audio_pulse_vu_bars.glsl"    },
};
static const int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

static QString loadResource(const char *path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

} // namespace

ShaderEditDialog::ShaderEditDialog(const QString &initialCode, QWidget *parent)
    : QDialog(parent), ui(new Ui::ShaderEditDialog)
{
    ui->setupUi(this);

    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);
    mono.setFixedPitch(true);
    ui->codeEdit->setFont(mono);
    ui->codeEdit->setTabStopDistance(28);

    for (int i = 0; i < kPresetCount; ++i)
        ui->presetList->addItem(kPresets[i].name);
    ui->presetList->addItem("Custom Shader");

    connect(ui->presetList,       &QListWidget::currentRowChanged,
            this,                 &ShaderEditDialog::onPresetSelected);
    connect(ui->refreshPreviewBtn,&QPushButton::clicked,
            this,                 &ShaderEditDialog::onCompilePreview);
    connect(ui->buttonBox,        &QDialogButtonBox::accepted,
            this,                 &QDialog::accept);
    connect(ui->buttonBox,        &QDialogButtonBox::rejected,
            this,                 &QDialog::reject);

    if (!initialCode.isEmpty()) {
        ui->codeEdit->setPlainText(initialCode);
        // Try to match against a preset so the list shows the right selection
        QString trimmed = initialCode.trimmed();
        int matchRow = kPresetCount; // "Custom Shader"
        for (int i = 0; i < kPresetCount; ++i) {
            if (loadResource(kPresets[i].resource) == trimmed) {
                matchRow = i;
                break;
            }
        }
        ui->presetList->blockSignals(true);
        ui->presetList->setCurrentRow(matchRow);
        ui->presetList->blockSignals(false);
        updatePreview();
    } else {
        ui->presetList->setCurrentRow(0);
        // onPresetSelected fires and populates code + preview
    }
}

ShaderEditDialog::~ShaderEditDialog() {
    delete ui;
}

QString ShaderEditDialog::resultCode() const {
    return ui->codeEdit->toPlainText();
}

void ShaderEditDialog::onPresetSelected(int row) {
    if (row < 0 || row >= kPresetCount) return;
    QString code = loadResource(kPresets[row].resource);
    if (!code.isEmpty())
        ui->codeEdit->setPlainText(code);
    updatePreview();
}

void ShaderEditDialog::onCompilePreview() {
    updatePreview();
}

void ShaderEditDialog::updatePreview() {
    QString code = ui->codeEdit->toPlainText().trimmed();
    if (code.isEmpty()) return;

    if (!m_previewSrc) {
        m_previewSrc = std::make_unique<ShaderSource>(code, QSize(320, 180));
    } else {
        m_previewSrc->setShaderCode(code);
    }

    m_previewSrc->nextFrame();

    if (!m_previewSrc->isReady()) {
        ui->previewLabel->setText("GL init failed");
        ui->errorLabel->setText("Could not initialize OpenGL offscreen context.");
        return;
    }

    if (!m_previewSrc->isCompiled()) {
        ui->errorLabel->setText(m_previewSrc->lastError());
    } else {
        ui->errorLabel->clear();
    }

    const uint8_t *data = m_previewSrc->frameData();
    QImage img(data, 320, 180, 320 * 3, QImage::Format_RGB888);
    ui->previewLabel->setPixmap(QPixmap::fromImage(img));
    ui->previewLabel->setText({});
}
