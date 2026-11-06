#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// App-wide QSettings preferences: theme, graphics API, hardware decode/encode, interface language
// and scale, and autosave. Exposed to QML as the PreferencesController singleton. Nothing here
// reads the open project.
class PreferencesController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // App-wide theme preference, backed by QSettings("ui/darkMode"). Until the user
    // toggles once, darkModeOverridden is false and the UI follows the OS colour scheme live;
    // after that the stored choice wins on every launch.
    Q_PROPERTY(bool darkModeOverridden READ darkModeOverridden NOTIFY darkModePreferenceChanged)
    Q_PROPERTY(bool darkModePreferred READ darkModePreferred NOTIFY darkModePreferenceChanged)

    // Hardware-accelerated decode of source media, QSettings("decode/hardware"). Takes effect
    // after restart.
    Q_PROPERTY(bool hardwareDecode READ hardwareDecode WRITE setHardwareDecode NOTIFY hardwareDecodeChanged)
    // Hardware-accelerated encode on export, QSettings("export/hardware"). Takes effect on the
    // next export.
    Q_PROPERTY(bool hardwareEncode READ hardwareEncode WRITE setHardwareEncode NOTIFY hardwareEncodeChanged)

    // Graphics API the interface is drawn with (GraphicsApiChoice.h), next launch. uiGraphicsApis
    // lists what this platform offers; uiGraphicsApiRevertedFrom names a choice this launch fell
    // back from after it failed to start.
    Q_PROPERTY(QString uiGraphicsApi READ uiGraphicsApi WRITE setUiGraphicsApi NOTIFY uiGraphicsApiChanged)
    Q_PROPERTY(QStringList uiGraphicsApis READ uiGraphicsApis CONSTANT)
    Q_PROPERTY(QString uiGraphicsApiRevertedFrom READ uiGraphicsApiRevertedFrom CONSTANT)

    // Recovery snapshot interval for unsaved work, QSettings("editor/autosaveIntervalSeconds").
    // 0 is off; the other choices are 15, 30, 60 and 300.
    Q_PROPERTY(int autosaveIntervalSeconds READ autosaveIntervalSeconds WRITE setAutosaveIntervalSeconds
                   NOTIFY autosaveIntervalSecondsChanged)

    // App-wide interface language, QSettings("ui/language"). Empty means follow the OS locale.
    // "en" is the source catalog (no .qm). Other codes match i18n/prism_<code>.qm.
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)
    Q_PROPERTY(QVariantList uiLanguages READ uiLanguages NOTIFY uiLanguageChanged)

    // QSettings("ui/scale"), 1.0..2.0 in 0.25 steps. Extra scale on top of the OS display scale,
    // except where applyStoredUiScale is given the OS scale (Windows): there it is the absolute
    // size, 1.0 being 96 DPI. Applied as QT_SCALE_FACTOR before QApplication; a change needs a
    // restart.
    Q_PROPERTY(double uiScale READ uiScale WRITE setUiScale NOTIFY uiScaleChanged)
    Q_PROPERTY(double appliedUiScale READ appliedUiScale CONSTANT)
    Q_PROPERTY(bool uiScaleNeedsRestart READ uiScaleNeedsRestart NOTIFY uiScaleChanged)

public:
    explicit PreferencesController(QObject *parent = nullptr);

    bool darkModeOverridden() const { return m_darkModeOverridden; }
    bool darkModePreferred() const { return m_darkModePreferred; }
    Q_INVOKABLE void setDarkModePreference(bool enabled);
    Q_INVOKABLE void clearDarkModePreference();

    bool hardwareDecode() const { return m_hardwareDecode; }
    void setHardwareDecode(bool enabled);
    bool hardwareEncode() const { return m_hardwareEncode; }
    void setHardwareEncode(bool enabled);

    QString uiGraphicsApi() const { return m_uiGraphicsApi; }
    void setUiGraphicsApi(const QString &id);
    QStringList uiGraphicsApis() const;
    QString uiGraphicsApiRevertedFrom() const;

    int autosaveIntervalSeconds() const { return m_autosaveIntervalSeconds; }
    void setAutosaveIntervalSeconds(int seconds);

    QString uiLanguage() const { return m_uiLanguage; }
    QVariantList uiLanguages() const;
    void setUiLanguage(const QString &language);

    double uiScale() const { return m_uiScale; }
    double appliedUiScale() const;
    bool uiScaleNeedsRestart() const;
    void setUiScale(double scale);
    // Snaps to 1.0, 1.25, 1.5, 1.75, or 2.0. Safe before any PreferencesController exists.
    static double storedUiScale();
    // Once per install: sets ui/scale from the primary screen's short side, 1080 rows being 1.0,
    // and records ui/autoScaleDone. Same preconditions as applyStoredUiScale.
    static void autoUiScaleOnce(int shortSide, double osDisplayScale);
    // Writes QT_SCALE_FACTOR from ui/scale unless the environment already set one. With
    // osDisplayScale, ui/scale is the absolute size and the factor is divided by it.
    // Call once before QApplication; organization/application names must already be set.
    static void applyStoredUiScale(double osDisplayScale = 0.0);
    // Installs the .qm for ui/language (or the system locale). Call once after QApplication
    // is named, and again from setUiLanguage. Safe before any PreferencesController exists.
    static void installUiTranslators();

signals:
    void darkModePreferenceChanged();
    void hardwareDecodeChanged();
    void hardwareEncodeChanged();
    void uiGraphicsApiChanged();
    void autosaveIntervalSecondsChanged();
    void uiLanguageChanged();
    void uiScaleChanged();
    // Settings that only apply after a restart. The UI shows this as a toast.
    void restartNoticeRequested(const QString &text);

private:
    bool m_darkModeOverridden = false;
    bool m_darkModePreferred = true;
    bool m_hardwareDecode = true;
    bool m_hardwareEncode = false;
    QString m_uiGraphicsApi;
    int m_autosaveIntervalSeconds = 15;
    QString m_uiLanguage;
    double m_uiScale = 1.0;
};
