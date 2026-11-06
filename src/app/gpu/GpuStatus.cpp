#include "app/gpu/GpuStatus.h"

#include "core/media/HwAccel.h"

#include <QMetaObject>

GpuStatus::GpuStatus(QObject *parent)
    : QObject(parent)
{
    // Called on whichever thread hit the failure; the queued hop is what makes that safe.
    prism::hwaccel::setFailureListener([this] {
        QMetaObject::invokeMethod(this, [this] { pull(); }, Qt::QueuedConnection);
    });
    // Anything reported before the shell first asked for this object. Queued, so a Connections
    // that is still being created when the singleton is instantiated sees it.
    if (prism::hwaccel::lastFailure().isValid())
        QMetaObject::invokeMethod(this, [this] { pull(); }, Qt::QueuedConnection);
}

GpuStatus::~GpuStatus()
{
    prism::hwaccel::setFailureListener({});
}

void GpuStatus::pull()
{
    const prism::hwaccel::AccelerationFailure failure = prism::hwaccel::lastFailure();
    if (!failure.isValid() || failure.serial == m_serial)
        return;
    m_serial = failure.serial;

    using Stage = prism::hwaccel::AccelerationFailure::Stage;
    m_stage = failure.stage == Stage::Encode ? QStringLiteral("encode") : QStringLiteral("decode");
    m_backend = failure.backend;
    m_detail = failure.detail;
    emit changed();

    if (m_reported.contains(m_stage + QLatin1Char(':') + m_backend))
        return;
    m_reported.insert(m_stage + QLatin1Char(':') + m_backend);
    emit accelerationFailed(m_stage, m_backend, m_detail);
}
