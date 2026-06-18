#include "ui/RecordingOptions.h"
#include "ui/ProgramRecorder.h"

QString RecordingOptions::effectiveOutputDir() const {
    return outputDir.isEmpty() ? ProgramRecorder::defaultOutputDir() : outputDir;
}
