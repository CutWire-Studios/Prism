#include "ui/recording/RecordingOptions.h"
#include "ui/recording/ProgramRecorder.h"

#include <QSettings>

namespace {
constexpr char kSettingsGroup[] = "Recording";
constexpr char kOutputDirKey[] = "outputDir";
}

QString RecordingOptions::effectiveOutputDir() const {
    return outputDir.isEmpty() ? ProgramRecorder::defaultOutputDir() : outputDir;
}

RecordingOptions RecordingOptions::loadSaved() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    RecordingOptions opts;
    opts.outputDir = settings.value(QLatin1String(kOutputDirKey), ProgramRecorder::defaultOutputDir()).toString();
    settings.endGroup();
    opts.videoCodec = ProgramRecorder::codecSetting();
    opts.container = ProgramRecorder::containerSetting();
    return opts;
}

bool RecordingOptions::hasChosenOutputDir() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const bool has = settings.contains(QLatin1String(kOutputDirKey));
    settings.endGroup();
    return has;
}

void RecordingOptions::saveOutputDir(const QString &dir) {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(QLatin1String(kOutputDirKey),
                      dir.isEmpty() ? ProgramRecorder::defaultOutputDir() : dir);
    settings.endGroup();
}
