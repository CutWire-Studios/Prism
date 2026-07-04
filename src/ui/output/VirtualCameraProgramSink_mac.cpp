#include "ui/output/VirtualCameraProgramSink.h"

// macOS has no built-in virtual-camera loopback: exposing one requires a
// signed CoreMediaIO DAL / Camera Extension shipped as a system extension,
// which is out of scope here. This stub reports the sink as unavailable so the
// "Virtual Camera Output" action stays disabled (MainWindow already greys it
// out and shows the "not available on this platform" tooltip when this returns
// false). Everything else in the program-output hub works normally.
// ponytail: stub — replace with a Camera Extension backend if mac virtual cam is needed.

struct VirtualCameraProgramSink::Impl {};

VirtualCameraProgramSink::VirtualCameraProgramSink()
    : m_impl(std::make_unique<Impl>())
{
}

VirtualCameraProgramSink::~VirtualCameraProgramSink() = default;

bool VirtualCameraProgramSink::isAvailable() const {
    return false;
}

QStringList VirtualCameraProgramSink::availableLoopbackDevices() {
    return {};
}

QString VirtualCameraProgramSink::defaultDevicePath() {
    return {};
}

bool VirtualCameraProgramSink::start(const QString &streamName) {
    Q_UNUSED(streamName);
    return false;
}

void VirtualCameraProgramSink::stopInternal() {
    m_active = false;
}

void VirtualCameraProgramSink::submitFrame(const QImage &frame) {
    Q_UNUSED(frame);
}
