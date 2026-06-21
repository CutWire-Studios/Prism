#include "ui/recording/RecordingOptions.h"
#include "ui/recording/ProgramRecorder.h"

QString RecordingOptions::effectiveOutputDir() const {
    return outputDir.isEmpty() ? ProgramRecorder::defaultOutputDir() : outputDir;
}
