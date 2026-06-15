#include "core/ScreenSource.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDBusArgument>
#include <QRandomGenerator>
#include <QDebug>

#include <unistd.h>  // dup()

static constexpr const char *PORTAL_SERVICE    = "org.freedesktop.portal.Desktop";
static constexpr const char *PORTAL_PATH       = "/org/freedesktop/portal/desktop";
static constexpr const char *SCREENCAST_IFACE  = "org.freedesktop.portal.ScreenCast";
static constexpr const char *REQUEST_IFACE     = "org.freedesktop.portal.Request";
static constexpr const char *SESSION_IFACE     = "org.freedesktop.portal.Session";

// Unique token safe for use in D-Bus object paths (alphanumeric + underscore).
static QString makeToken(const QString &prefix) {
    return QStringLiteral("%1_%2").arg(prefix).arg(QRandomGenerator::global()->generate());
}

// D-Bus sender name, with ':' stripped and '.' replaced by '_'.
// Used to construct request/session object paths.
static QString senderName() {
    return QDBusConnection::sessionBus().baseService().mid(1).replace('.', '_');
}

// ─────────────────────────────────────────────────────────────────────────────

ScreenSource::ScreenSource() {
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
}

ScreenSource::~ScreenSource() {
    stop();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool ScreenSource::start(CaptureType type) {
    if (m_state != State::Idle)
        stop();

    m_captureType = type;

    QString reqToken = makeToken("req");
    QString sesToken = makeToken("ses");

    // Build the request path *before* the call so we can subscribe to the
    // Response signal before the reply arrives (avoids a race).
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

// ── Portal handshake — step 1: CreateSession response ────────────────────────

void ScreenSource::onCreateSessionResponse(uint response, QVariantMap results) {
    if (response != 0) {
        qWarning() << "ScreenSource: CreateSession rejected (code" << response << ")";
        m_state = State::Idle;
        return;
    }

    m_sessionHandle = results.value("session_handle").toString();
    qDebug() << "ScreenSource: session_handle =" << m_sessionHandle;

    // Step 2: SelectSources
    QString reqToken = makeToken("req");
    QString reqPath  = QStringLiteral(
        "/org/freedesktop/portal/desktop/request/%1/%2").arg(senderName(), reqToken);

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(PORTAL_SERVICE, reqPath, REQUEST_IFACE, "Response",
                this, SLOT(onSelectSourcesResponse(uint,QVariantMap)));

    QVariantMap opts;
    opts["handle_token"] = reqToken;
    opts["types"]        = static_cast<uint>(m_captureType);  // 1=Monitor, 2=Window, 3=Any
    opts["multiple"]     = false;
    opts["cursor_mode"]  = uint(2);   // 2=embedded cursor

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

// ── Portal handshake — step 2: SelectSources response ────────────────────────

void ScreenSource::onSelectSourcesResponse(uint response, QVariantMap /*results*/) {
    if (response != 0) {
        qWarning() << "ScreenSource: SelectSources rejected (code" << response << ")";
        m_state = State::Idle;
        return;
    }

    // Step 3: Start
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
                                    QString(),  // parent window handle (empty = no parent)
                                    opts);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "ScreenSource: Start error:" << reply.errorMessage();
        m_state = State::Idle;
        return;
    }

    m_state = State::Starting;
}

// ── Portal handshake — step 3: Start response ────────────────────────────────

void ScreenSource::onStartResponse(uint response, QVariantMap results) {
    if (response != 0) {
        qWarning() << "ScreenSource: Start rejected (code" << response << ")";
        m_state = State::Idle;
        return;
    }

    // Extract the first PipeWire node ID from the streams array (type a(ua{sv})).
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
    qDebug() << "ScreenSource: PipeWire node_id =" << nodeId;

    // Step 4: OpenPipeWireRemote — get the PipeWire socket fd.
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

    // dup() the fd so we own a copy that outlives the QDBusUnixFileDescriptor.
    QDBusUnixFileDescriptor ufd = reply.arguments().first().value<QDBusUnixFileDescriptor>();
    int fd = dup(ufd.fileDescriptor());
    qDebug() << "ScreenSource: PipeWire fd =" << fd;

    buildGstPipeline(fd, nodeId);
}

// ── GStreamer pipeline ────────────────────────────────────────────────────────

void ScreenSource::buildGstPipeline(int fd, uint32_t nodeId) {
    // pipewiresrc reads the PipeWire stream identified by (fd, path/node_id),
    // videoconvert normalises pixel format, appsink delivers frames to us.
    QString desc = QStringLiteral(
        "pipewiresrc fd=%1 path=%2 do-timestamp=true ! "
        "videoconvert ! video/x-raw,format=RGB ! "
        "appsink name=sink emit-signals=true sync=false max-buffers=2 drop=true"
    ).arg(fd).arg(nodeId);

    qDebug() << "ScreenSource: pipeline:" << desc;

    GError *err = nullptr;
    m_pipeline = gst_parse_launch(desc.toUtf8().constData(), &err);
    if (!m_pipeline || err) {
        qWarning() << "ScreenSource: pipeline parse error:"
                   << (err ? err->message : "unknown");
        if (err) g_error_free(err);
        m_state = State::Idle;
        return;
    }

    m_appsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "sink");
    g_signal_connect(m_appsink, "new-sample", G_CALLBACK(onNewSample), this);

    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    m_state = State::Capturing;
    m_name  = "Portal Screen";
    qDebug() << "ScreenSource: pipeline playing";
}

// Called on a GStreamer streaming thread — must not touch m_frame directly.
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
            // Deep-copy before unmapping so the QImage owns its data.
            QImage img(map.data, w, h, w * 3, QImage::Format_RGB888);
            QImage copy = img.copy();
            gst_buffer_unmap(buffer, &map);

            // Marshal frame delivery to the main thread.
            QMetaObject::invokeMethod(self, [self, frame = std::move(copy)]() mutable {
                self->m_frame = std::move(frame);
                self->m_dirty = true;
            }, Qt::QueuedConnection);
        }
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}
