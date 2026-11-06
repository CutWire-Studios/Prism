#include "app/editor/PreferencesController.h"
#include "app/GraphicsApiChoice.h"
#include "app/gpu/GpuPreference.h"
#include "core/media/HwAccel.h"
#include "core/media/VideoDecoder.h"
#include "core/sources/VideoFileSource.h"
#include "app/recording/ProgramRecorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

#include <cmath>
#include <limits>

namespace {

QTranslator g_appTranslator;
QTranslator g_qtTranslator;

// The OS display scale ui/scale is measured against. 1.0 leaves ui/scale relative to the OS
// scale; Windows passes its own, which makes ui/scale the absolute size.
double g_osDisplayScale = 1.0;

QString storedUiLanguage()
{
    return QSettings().value(QStringLiteral("ui/language")).toString().trimmed();
}

int normalizedAutosaveInterval(int seconds)
{
    return seconds == 0 || seconds == 15 || seconds == 30 || seconds == 60 || seconds == 300
        ? seconds
        : 15;
}

double normalizeUiScale(double scale)
{
    static const double kSteps[] = {1.0, 1.25, 1.5, 1.75, 2.0};
    double best = 1.0;
    double bestDist = std::numeric_limits<double>::max();
    for (double step : kSteps) {
        const double dist = std::abs(scale - step);
        if (dist < bestDist) {
            best = step;
            bestDist = dist;
        }
    }
    return best;
}

double appliedUiScaleFromEnvironment()
{
    bool ok = false;
    const double env = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&ok);
    if (ok && env > 0.0)
        return env * g_osDisplayScale;
    return g_osDisplayScale;
}

QLocale uiLocaleFromStored()
{
    const QString code = storedUiLanguage();
    return code.isEmpty() ? QLocale::system() : QLocale(code);
}

QString languageLabel(const QString &code)
{
    const QLocale loc(code);
    QString native = loc.nativeLanguageName();
    if (native.isEmpty())
        return code;
    if (code.contains(QLatin1Char('_')) || code.contains(QLatin1Char('-'))) {
        const QString territory = loc.nativeTerritoryName();
        if (!territory.isEmpty())
            native += QStringLiteral(" (%1)").arg(territory);
    }
    return native;
}

} // namespace

PreferencesController::PreferencesController(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    const QString decodeMode = VideoDecoder::modeSetting();
    m_hardwareDecode = decodeMode != QLatin1String("software");
    if (decodeMode.startsWith(QLatin1String("hw:"))
        && prism::hwaccel::backendFromId(decodeMode.mid(3)) != prism::hwaccel::Backend::None)
        m_decodeBackend = decodeMode.mid(3);
    m_hardwareEncode = ProgramRecorder::encoderSetting() != QLatin1String("software");
    m_gpuPreference = prism::gpu::preferenceId(prism::gpu::storedPreference());
    m_uiGraphicsApi = prism::graphics::storedApi();
    m_autosaveIntervalSeconds =
        normalizedAutosaveInterval(settings.value(QStringLiteral("editor/autosaveIntervalSeconds"), 15).toInt());
    m_uiLanguage = storedUiLanguage();
    m_uiScale = storedUiScale();
    // Unset means the user has never toggled the theme, so the UI keeps tracking the OS.
    const QVariant storedDarkMode = settings.value(QStringLiteral("ui/darkMode"));
    m_darkModeOverridden = storedDarkMode.isValid();
    m_darkModePreferred = storedDarkMode.toBool();
}

void PreferencesController::setDarkModePreference(bool enabled)
{
    if (m_darkModeOverridden && m_darkModePreferred == enabled)
        return;
    m_darkModeOverridden = true;
    m_darkModePreferred = enabled;
    QSettings().setValue(QStringLiteral("ui/darkMode"), m_darkModePreferred);
    emit darkModePreferenceChanged();
}

void PreferencesController::clearDarkModePreference()
{
    if (!m_darkModeOverridden)
        return;
    m_darkModeOverridden = false;
    QSettings().remove(QStringLiteral("ui/darkMode"));
    emit darkModePreferenceChanged();
}

void PreferencesController::applyDecodeMode()
{
    const QString mode = !m_hardwareDecode ? QStringLiteral("software")
        : m_decodeBackend == QLatin1String("auto") ? QStringLiteral("auto")
                                                   : QStringLiteral("hw:") + m_decodeBackend;
    if (mode == VideoDecoder::modeSetting())
        return;
    VideoDecoder::storeModeSetting(mode);
    VideoDecoder::applyModeSetting(mode);
    VideoFileSource::reopenAll();
}

void PreferencesController::setHardwareDecode(bool enabled)
{
    if (m_hardwareDecode == enabled)
        return;
    m_hardwareDecode = enabled;
    applyDecodeMode();
    emit hardwareDecodeChanged();
}

void PreferencesController::setDecodeBackend(const QString &id)
{
    const QString normalized = prism::hwaccel::backendFromId(id) == prism::hwaccel::Backend::None
        ? QStringLiteral("auto") : id;
    if (m_decodeBackend == normalized)
        return;
    m_decodeBackend = normalized;
    applyDecodeMode();
    emit hardwareDecodeChanged();
}

QVariantList PreferencesController::decodeBackends() const
{
    static const QVariantList backends = [] {
        QVariantList list;
        for (const prism::hwaccel::Backend backend : prism::hwaccel::availableDecodeBackends()) {
            list.append(QVariantMap{
                {QStringLiteral("id"), prism::hwaccel::id(backend)},
                {QStringLiteral("label"), QString::fromLatin1(prism::hwaccel::name(backend))},
            });
        }
        return list;
    }();
    return backends;
}

QString PreferencesController::decodeBackendWarning() const
{
    const prism::hwaccel::Backend backend = prism::hwaccel::backendFromId(m_decodeBackend);
    if (!m_hardwareDecode || backend == prism::hwaccel::Backend::None)
        return {};
    const prism::hwaccel::RenderMatchInfo match = prism::hwaccel::describeRenderMatch(backend);
    if (match.match != prism::hwaccel::RenderMatch::Mismatch)
        return {};
    return tr("%1 decodes on %2 while Prism draws on %3, so every frame crosses between GPUs. "
              "Automatic avoids this.")
        .arg(QString::fromLatin1(prism::hwaccel::name(backend)),
             match.decodeGpu.isEmpty() ? tr("another GPU") : match.decodeGpu,
             match.renderGpu.isEmpty() ? tr("this GPU") : match.renderGpu);
}

void PreferencesController::setHardwareEncode(bool enabled)
{
    if (m_hardwareEncode == enabled)
        return;
    m_hardwareEncode = enabled;
    ProgramRecorder::setEncoderSetting(enabled ? QStringLiteral("auto") : QStringLiteral("software"));
    emit hardwareEncodeChanged();
}

void PreferencesController::setGpuPreference(const QString &id)
{
    const prism::gpu::Preference preference = prism::gpu::preferenceFromId(id);
    const QString normalized = prism::gpu::preferenceId(preference);
    if (m_gpuPreference == normalized)
        return;
    m_gpuPreference = normalized;
    prism::gpu::storePreference(preference);
    emit gpuPreferenceChanged();
    // Not conservative advice: the driver chose this process's GPU when it loaded.
    emit restartNoticeRequested(tr("The graphics card choice takes effect after you restart Prism."));
}

bool PreferencesController::gpuPreferenceSupported() const
{
    return prism::gpu::preferenceSupported();
}

bool PreferencesController::gpuPreferenceInSystemSettings() const
{
    return prism::gpu::multipleAdapters() && prism::gpu::packagedApp();
}

void PreferencesController::setUiGraphicsApi(const QString &id)
{
    if (m_uiGraphicsApi == id || !prism::graphics::availableApis().contains(id))
        return;
    m_uiGraphicsApi = id;
    prism::graphics::storeApi(id);
    emit uiGraphicsApiChanged();
    emit restartNoticeRequested(tr("The interface graphics API takes effect after you restart Prism."));
}

QStringList PreferencesController::uiGraphicsApis() const
{
    return prism::graphics::availableApis();
}

QString PreferencesController::uiGraphicsApiRevertedFrom() const
{
    return prism::graphics::revertedFrom();
}

void PreferencesController::setAutosaveIntervalSeconds(int seconds)
{
    seconds = normalizedAutosaveInterval(seconds);
    if (m_autosaveIntervalSeconds == seconds)
        return;
    m_autosaveIntervalSeconds = seconds;
    QSettings().setValue(QStringLiteral("editor/autosaveIntervalSeconds"), m_autosaveIntervalSeconds);
    emit autosaveIntervalSecondsChanged();
}

void PreferencesController::installUiTranslators()
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;

    app->removeTranslator(&g_appTranslator);
    app->removeTranslator(&g_qtTranslator);

    const QLocale locale = uiLocaleFromStored();
    QLocale::setDefault(locale);
    QGuiApplication::setLayoutDirection(locale.textDirection());

    const QString translationsDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (g_qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), translationsDir))
        app->installTranslator(&g_qtTranslator);

    if (locale.language() == QLocale::English)
        return;

    if (g_appTranslator.load(locale, QStringLiteral("prism"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        app->installTranslator(&g_appTranslator);
}

QVariantList PreferencesController::availableLanguages() const
{
    QVariantList languages;

    QVariantMap system;
    system.insert(QStringLiteral("id"), QString());
    system.insert(QStringLiteral("label"), tr("System default"));
    languages.append(system);

    QVariantMap english;
    english.insert(QStringLiteral("id"), QStringLiteral("en"));
    english.insert(QStringLiteral("label"), languageLabel(QStringLiteral("en")));
    languages.append(english);

    QStringList codes;
    const QDir dir(QStringLiteral(":/i18n"));
    const QStringList files = dir.entryList({QStringLiteral("prism_*.qm")}, QDir::Files);
    for (const QString &file : files) {
        const QString code = file.mid(6, file.size() - 9); // strip prism_ and .qm
        if (code.isEmpty() || code.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0)
            continue;
        if (!codes.contains(code))
            codes.append(code);
    }
    codes.sort(Qt::CaseInsensitive);
    for (const QString &code : codes) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), code);
        entry.insert(QStringLiteral("label"), languageLabel(code));
        languages.append(entry);
    }
    return languages;
}

void PreferencesController::setUiLanguage(const QString &language)
{
    const QString normalized = language.trimmed();
    if (m_uiLanguage == normalized)
        return;
    m_uiLanguage = normalized;
    QSettings settings;
    if (m_uiLanguage.isEmpty())
        settings.remove(QStringLiteral("ui/language"));
    else
        settings.setValue(QStringLiteral("ui/language"), m_uiLanguage);
    installUiTranslators();
    emit uiLanguageChanged();
}

double PreferencesController::storedUiScale()
{
    const QVariant stored = QSettings().value(QStringLiteral("ui/scale"));
    if (!stored.isValid())
        return 1.0;
    bool ok = false;
    const double value = stored.toDouble(&ok);
    if (!ok)
        return 1.0;
    return normalizeUiScale(value);
}

void PreferencesController::autoUiScaleOnce(int shortSide, double osDisplayScale)
{
    QSettings settings;
    if (settings.value(QStringLiteral("ui/autoScaleDone")).toBool())
        return;
    settings.setValue(QStringLiteral("ui/autoScaleDone"), true);
    if (shortSide <= 0 || osDisplayScale <= 0.0)
        return;
    // The layout is designed at 1080 rows; keep that much of the UI on screen at any resolution.
    const double scale = normalizeUiScale(shortSide / 1080.0);
    qInfo("PreferencesController: auto interface size %.0f%% for %d rows at %.0f%% display scale",
          scale * 100.0, shortSide, osDisplayScale * 100.0);
    if (qFuzzyCompare(scale, 1.0))
        settings.remove(QStringLiteral("ui/scale"));
    else
        settings.setValue(QStringLiteral("ui/scale"), scale);
}

void PreferencesController::applyStoredUiScale(double osDisplayScale)
{
    if (osDisplayScale > 0.0)
        g_osDisplayScale = osDisplayScale;
    // A shell or .desktop Exec=QT_SCALE_FACTOR=... stays the escape hatch.
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR"))
        return;
    const double factor = storedUiScale() / g_osDisplayScale;
    if (qFuzzyCompare(factor, 1.0))
        return;
    qputenv("QT_SCALE_FACTOR", QByteArray::number(factor, 'g', 4));
}

double PreferencesController::appliedUiScale() const
{
    return appliedUiScaleFromEnvironment();
}

bool PreferencesController::uiScaleNeedsRestart() const
{
    return !qFuzzyCompare(m_uiScale, appliedUiScaleFromEnvironment());
}

void PreferencesController::setUiScale(double scale)
{
    const double normalized = normalizeUiScale(scale);
    if (qFuzzyCompare(m_uiScale, normalized))
        return;
    m_uiScale = normalized;
    QSettings settings;
    if (qFuzzyCompare(m_uiScale, 1.0))
        settings.remove(QStringLiteral("ui/scale"));
    else
        settings.setValue(QStringLiteral("ui/scale"), m_uiScale);
    emit uiScaleChanged();
}
