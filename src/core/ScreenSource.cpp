#include "core/ScreenSource.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDBusArgument>
#include <QRandomGenerator>
#include <QApplication>
#include <QMessageBox>
#include <QDebug>

#include <unistd.h>

static constexpr const char *PORTAL_SERVICE   = "org.freedesktop.portal.Desktop";
static constexpr const char *PORTAL_PATH      = "/org/freedesktop/portal/desktop";
static constexpr const char *SCREENCAST_IFACE = "org.freedesktop.portal.ScreenCast";
static constexpr const char *REQUEST_IFACE    = "org.freedesktop.portal.Request";
static constexpr const char *SESSION_IFACE    = "org.freedesktop.portal.Session";

static QString makeToken(const QString &prefix) {
    return QStringLiteral("%1_%2").arg(prefix).arg(QRandomGenerator::global()->generate());
}

static QString senderName() {
    return QDBusConnection::sessionBus().baseService().mid(1).replace('.', '_');
}

ScreenSource::ScreenSource() {
    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);
}

ScreenSource::~ScreenSource() {
    stop();
}

bool ScreenSource::start(CaptureType type) {
    if (m_state != State::Idle)
        stop();

    m_captureType = type;

    QString reqToken = makeToken("req");
    QString sesToken = makeToken("ses");

    QString requestPath = QStringLiteral(
        "/org/freedesktop/portal/desktop/request/%1/%2").arg(senderName(), reqToken);

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(PORTAL_SERVICE, requestPath, REQUEST_IFACE, "Response",
                this, SLOT(onCreateSessionResponse(uint,QVariantMap)));

    QDBusInterface iface(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE, bus);
    if (!iface.isValid()) {
        qWarning() << "ScreenSource: org.freedesktop.portal.ScreenCast not available";
        bus.disconnect(PORTAL_SERVICE, requestPath, REQUEST_IFACE, "Response",
                       this, SLOT(onCreateSessionResponse(uint,QVariantMap)));
        return false;
    }

    QVariantMap opts;
    opts["handle_token"]         = reqToken;
    opts["session_handle_token"] = sesToken;

    QDBusMessage reply = iface.call("CreateSession", opts);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "ScreenSource: CreateSession error:" << reply.errorMessage();
        bus.disconnect(PORTAL_SERVICE, requestPath, REQUEST_IFACE, "Response",
                       this, SLOT(onCreateSessionResponse(uint,QVariantMap)));
        return false;
    }

    qDebug() << "ScreenSource: CreateSession sent — awaiting portal picker…";
    m_state = State::CreatingSession;
    return true;
}

void ScreenSource::stop() {
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        if (m_appsink) {
            gst_object_unref(m_appsink);
            m_appsink = nullptr;
        }
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    if (!m_sessionHandle.isEmpty()) {
        QDBusInterface session(PORTAL_SERVICE, m_sessionHandle, SESSION_IFACE,
                               QDBusConnection::sessionBus());
        session.call("Close");
        m_sessionHandle.clear();
    }

    m_frame = {};
    m_dirty = false;
    m_state = State::Idle;
    m_name.clear();
}

bool ScreenSource::isCapturing() const {
    return m_state == State::Capturing;
}

bool ScreenSource::nextFrame() {
    if (!m_dirty) return false;
    m_dirty = false;
    return true;
}

void ScreenSource::onCreateSessionResponse(uint response, QVariantMap results) {
    if (response != 0) {
        qWarning() << "ScreenSource: CreateSession rejected (code" << response << ")";
        m_state = State::Idle;
        return;
    }

    m_sessionHandle = results.value("session_handle").toString();

    QString reqToken = makeToken("req");
    QString reqPath  = QStringLiteral(
        "/org/freedesktop/portal/desktop/request/%1/%2").arg(senderName(), reqToken);

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(PORTAL_SERVICE, reqPath, REQUEST_IFACE, "Response",
                this, SLOT(onSelectSourcesResponse(uint,QVariantMap)));

    QVariantMap opts;
    opts["handle_token"] = reqToken;
    opts["types"]        = static_cast<uint>(m_captureType);
    opts["multiple"]     = false;
    opts["cursor_mode"]  = uint(2);

    QDBusInterface iface(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE, bus);
    QDBusMessage reply = iface.call("SelectSources",
                                    QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
                                    opts);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "ScreenSource: SelectSources error:" << reply.errorMessage();
        m_state = State::Idle;
        return;
    }

    m_state = State::SelectingSources;
}

void ScreenSource::onSelectSourcesResponse(uint response, QVariantMap) {
    if (response != 0) {
        qWarning() << "ScreenSource: SelectSources rejected (code" << response << ")";
        m_state = State::Idle;
        return;
    }

    QString reqToken = makeToken("req");
    QString reqPath  = QStringLiteral(
        "/org/freedesktop/portal/desktop/request/%1/%2").arg(senderName(), reqToken);

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(PORTAL_SERVICE, reqPath, REQUEST_IFACE, "Response",
                this, SLOT(onStartResponse(uint,QVariantMap)));

    QVariantMap opts;
    opts["handle_token"] = reqToken;

    QDBusInterface iface(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE, bus);
    QDBusMessage reply = iface.call("Start",
                                    QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
                                    QString(),
                                    opts);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "ScreenSource: Start error:" << reply.errorMessage();
        m_state = State::Idle;
        return;
    }

    m_state = State::Starting;
}

void ScreenSource::onStartResponse(uint response, QVariantMap results) {
    if (response != 0) {
        qWarning() << "ScreenSource: Start rejected (code" << response << ")";
        m_state = State::Idle;
        return;
    }

    uint32_t nodeId = 0;
    if (results.contains("streams")) {
        const QDBusArgument &arg = results["streams"].value<QDBusArgument>();
        arg.beginArray();
        if (!arg.atEnd()) {
            uint id;
            QVariantMap props;
            arg.beginStructure();
            arg >> id >> props;
            arg.endStructure();
            nodeId = id;
        }
        arg.endArray();
    }

    QDBusInterface iface(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE,
                         QDBusConnection::sessionBus());
    QDBusMessage reply = iface.call("OpenPipeWireRemote",
                                    QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
                                    QVariantMap{});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "ScreenSource: OpenPipeWireRemote error:" << reply.errorMessage();
        m_state = State::Idle;
        return;
    }

    QDBusUnixFileDescriptor ufd = reply.arguments().first().value<QDBusUnixFileDescriptor>();
    int fd = dup(ufd.fileDescriptor());
    buildGstPipeline(fd, nodeId);
}

void ScreenSource::buildGstPipeline(int fd, uint32_t nodeId) {
    QString desc = QStringLiteral(
        "pipewiresrc fd=%1 path=%2 do-timestamp=true ! "
        "videoconvert ! video/x-raw,format=RGB ! "
        "appsink name=sink emit-signals=true sync=false max-buffers=2 drop=true"
    ).arg(fd).arg(nodeId);

    GError *err = nullptr;
    m_pipeline = gst_parse_launch(desc.toUtf8().constData(), &err);
    if (!m_pipeline || err) {
        qWarning() << "ScreenSource: pipeline parse error:" << (err ? err->message : "unknown");
        if (err) g_error_free(err);
        m_state = State::Idle;
        return;
    }

    m_appsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "sink");
    g_signal_connect(m_appsink, "new-sample", G_CALLBACK(onNewSample), this);

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_add_watch(bus, onBusMessage, this);
    gst_object_unref(bus);

    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    m_state = State::Capturing;
    m_name  = "Portal Screen";
}

GstFlowReturn ScreenSource::onNewSample(GstAppSink *sink, gpointer userData) {
    auto *self = static_cast<ScreenSource *>(userData);

    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer    *buffer = gst_sample_get_buffer(sample);
    GstCaps      *caps   = gst_sample_get_caps(sample);
    GstStructure *st     = gst_caps_get_structure(caps, 0);

    int w = 0, h = 0;
    gst_structure_get_int(st, "width",  &w);
    gst_structure_get_int(st, "height", &h);

    if (w > 0 && h > 0) {
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            QImage img(map.data, w, h, w * 3, QImage::Format_RGB888);
            QImage copy = img.copy();
            gst_buffer_unmap(buffer, &map);

            QMetaObject::invokeMethod(self, [self, frame = std::move(copy)]() mutable {
                self->m_frame = std::move(frame);
                self->m_dirty = true;
            }, Qt::QueuedConnection);
        }
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

gboolean ScreenSource::onBusMessage(GstBus *bus, GstMessage *msg, gpointer userData) {
    Q_UNUSED(bus);
    auto *self = static_cast<ScreenSource *>(userData);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        const QString detail = QStringLiteral("%1 (%2)")
            .arg(QString::fromUtf8(err ? err->message : "unknown"),
                 QString::fromUtf8(debug ? debug : ""));
        if (err) g_error_free(err);
        g_free(debug);
        QMetaObject::invokeMethod(self, [self, detail]() {
            self->handlePipelineError(detail);
        }, Qt::QueuedConnection);
        break;
    }
    case GST_MESSAGE_EOS:
        QMetaObject::invokeMethod(self, [self]() {
            self->handlePipelineError(ScreenSource::tr("Screen capture stream ended."));
        }, Qt::QueuedConnection);
        break;
    default:
        break;
    }
    return TRUE;
}

void ScreenSource::handlePipelineError(const QString &detail) {
    qWarning() << "ScreenSource: pipeline error:" << detail;
    if (m_state == State::Capturing || m_state == State::Starting) {
        if (QWidget *w = QApplication::activeWindow()) {
            QMessageBox::warning(w, tr("Screen Capture Error"),
                tr("Screen capture failed.\n\n"
                   "Try selecting the source again in the portal picker, "
                   "or check that PipeWire and xdg-desktop-portal are running.\n\n"
                   "Technical detail: %1").arg(detail));
        }
    }
    stop();
}
