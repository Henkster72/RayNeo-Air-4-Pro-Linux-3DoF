#include "portal_capture.h"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <gio/gio.h>

#include <pipewire/pipewire.h>
#include <spa/buffer/buffer.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/raw-utils.h>
#include <spa/pod/builder.h>

namespace
{
constexpr const char *kPortalBus = "org.freedesktop.portal.Desktop";
constexpr const char *kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char *kRemoteDesktop = "org.freedesktop.portal.RemoteDesktop";
constexpr const char *kScreenCast = "org.freedesktop.portal.ScreenCast";
constexpr const char *kRequest = "org.freedesktop.portal.Request";
constexpr const char *kSession = "org.freedesktop.portal.Session";
constexpr uint32_t kPointerDevice = 2;
constexpr uint32_t kMonitorSource = 1;
constexpr uint32_t kWindowSource = 2;
constexpr uint32_t kCursorModeHidden = 1;
constexpr uint32_t kCursorModeEmbedded = 2;

struct RequestResult
{
    GMainLoop *loop{nullptr};
    bool complete{false};
    uint32_t response{2};
    GVariant *results{nullptr};
};

static void onRequestResponse(GDBusConnection *, const gchar *, const gchar *,
                              const gchar *, const gchar *, GVariant *parameters,
                              gpointer userData)
{
    auto *result = static_cast<RequestResult *>(userData);
    guint response = 2;
    GVariant *values = nullptr;
    g_variant_get(parameters, "(u@a{sv})", &response, &values);
    result->response = response;
    result->results = values;
    result->complete = true;
    g_main_loop_quit(result->loop);
}

static bool waitForRequest(GDBusConnection *connection, const std::string &path,
                           RequestResult &result, std::string &error)
{
    result.loop = g_main_loop_new(nullptr, FALSE);
    const guint subscription = g_dbus_connection_signal_subscribe(
        connection, kPortalBus, kRequest, "Response", path.c_str(), nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, onRequestResponse, &result, nullptr);
    g_main_loop_run(result.loop);
    g_dbus_connection_signal_unsubscribe(connection, subscription);
    g_main_loop_unref(result.loop);
    result.loop = nullptr;
    if (!result.complete || result.response != 0)
    {
        error = result.response == 1 ? "KDE portal request was cancelled" :
                                      "KDE portal request was denied or failed";
        return false;
    }
    return true;
}

static GVariant *emptyOptions()
{
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    return g_variant_builder_end(&options);
}

static GVariant *sessionOptions(const std::string &requestToken,
                                const std::string &sessionToken)
{
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token",
                          g_variant_new_string(requestToken.c_str()));
    g_variant_builder_add(&options, "{sv}", "session_handle_token",
                          g_variant_new_string(sessionToken.c_str()));
    return g_variant_builder_end(&options);
}

static GVariant *pointerOptions(const std::string &requestToken)
{
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token",
                          g_variant_new_string(requestToken.c_str()));
    g_variant_builder_add(&options, "{sv}", "types",
                          g_variant_new_uint32(kPointerDevice));
    return g_variant_builder_end(&options);
}

static GVariant *sourceOptions(const std::string &requestToken, uint32_t sourceType,
                               uint32_t cursorMode, bool multiple = false)
{
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token",
                          g_variant_new_string(requestToken.c_str()));
    g_variant_builder_add(&options, "{sv}", "types",
                          g_variant_new_uint32(sourceType));
    g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(multiple));
    g_variant_builder_add(&options, "{sv}", "cursor_mode",
                          g_variant_new_uint32(cursorMode));
    return g_variant_builder_end(&options);
}

static std::string token(const char *kind)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string("rayneo_") + kind + "_" + std::to_string(now);
}

static bool callRequest(GDBusConnection *connection, const char *interface,
                        const char *method, GVariant *parameters,
                        std::string &path, std::string &error)
{
    GError *gerror = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(
        connection, kPortalBus, kPortalPath, interface, method, parameters,
        G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &gerror);
    if (!reply)
    {
        error = gerror ? gerror->message : "D-Bus portal call failed";
        if (gerror)
            g_error_free(gerror);
        return false;
    }
    const gchar *requestPath = nullptr;
    g_variant_get(reply, "(&o)", &requestPath);
    path = requestPath ? requestPath : "";
    g_variant_unref(reply);
    if (path.empty())
    {
        error = "KDE portal returned an empty request path";
        return false;
    }
    return true;
}

static bool requestAndWait(GDBusConnection *connection, const char *interface,
                           const char *method, GVariant *parameters,
                           RequestResult &result, std::string &error)
{
    std::string requestPath;
    if (!callRequest(connection, interface, method, parameters, requestPath, error))
        return false;
    return waitForRequest(connection, requestPath, result, error);
}

static std::string resultObjectPath(GVariant *results, const char *key)
{
    GVariant *value = g_variant_lookup_value(results, key, nullptr);
    if (!value)
        return {};
    const gchar *text = g_variant_get_string(value, nullptr);
    std::string path = text ? text : "";
    g_variant_unref(value);
    return path;
}

static uint32_t resultUint(GVariant *values, const char *key, uint32_t fallback)
{
    uint32_t value = fallback;
    g_variant_lookup(values, key, "u", &value);
    return value;
}
}

struct PortalCapture::Impl
{
    struct Stream
    {
        PortalFrame frame;
        std::mutex mutex;
        struct spa_video_info_raw format{};
        struct pw_stream *pipewire{nullptr};
        struct spa_hook listener{};
        bool haveFormat{false};
        bool ready{false};
    };

    GDBusConnection *connection{nullptr};
    std::vector<std::string> sessionPaths;
    std::string pointerSessionPath;
    std::vector<std::unique_ptr<Stream>> streams;
    struct pw_thread_loop *loop{nullptr};
    struct pw_context *context{nullptr};
    std::vector<struct pw_core *> cores;
    bool loopStarted{false};
    bool pipewireInitialized{false};

    static void onParamChanged(void *data, uint32_t id, const struct spa_pod *param)
    {
        auto *stream = static_cast<Stream *>(data);
        if (id != SPA_PARAM_Format || !param)
            return;
        struct spa_video_info_raw format{};
        if (spa_format_video_raw_parse(param, &format) < 0)
            return;
        std::lock_guard<std::mutex> lock(stream->mutex);
        stream->format = format;
        stream->haveFormat = true;
    }

    static void onProcess(void *data)
    {
        auto *stream = static_cast<Stream *>(data);
        struct pw_buffer *buffer = pw_stream_dequeue_buffer(stream->pipewire);
        if (!buffer)
            return;
        struct spa_buffer *spaBuffer = buffer->buffer;
        if (spaBuffer->n_datas > 0)
        {
            struct spa_data &dataPlane = spaBuffer->datas[0];
            struct spa_chunk *chunk = dataPlane.chunk;
            std::lock_guard<std::mutex> lock(stream->mutex);
            if (stream->haveFormat && dataPlane.data && chunk && chunk->size > 0 &&
                chunk->stride > 0 && stream->format.size.width > 0 &&
                stream->format.size.height > 0)
            {
                const int height = static_cast<int>(stream->format.size.height);
                const int stride = chunk->stride;
                const size_t size = static_cast<size_t>(stride) * height;
                if (size <= chunk->size && dataPlane.chunk->offset + size <= dataPlane.maxsize)
                {
                    const uint8_t *pixels = static_cast<const uint8_t *>(dataPlane.data) + chunk->offset;
                    stream->frame.width = static_cast<int>(stream->format.size.width);
                    stream->frame.height = height;
                    stream->frame.stride = stride;
                    stream->frame.bgra = stream->format.format == SPA_VIDEO_FORMAT_BGRx ||
                                         stream->format.format == SPA_VIDEO_FORMAT_BGRA;
                    stream->frame.pixels.assign(pixels, pixels + size);
                    stream->ready = true;
                }
            }
        }
        pw_stream_queue_buffer(stream->pipewire, buffer);
    }

    static void onStateChanged(void *, enum pw_stream_state, enum pw_stream_state state,
                               const char *error)
    {
        if (state == PW_STREAM_STATE_ERROR && error)
            std::printf("PipeWire stream error: %s\n", error);
    }

    bool startPipeWire(int fd, std::size_t firstStream, bool startLoop, std::string &error)
    {
        if (!pipewireInitialized)
        {
            pw_init(nullptr, nullptr);
            pipewireInitialized = true;
            loop = pw_thread_loop_new("rayneo-portal", nullptr);
            if (!loop)
            {
                error = "Could not create PipeWire thread loop";
                return false;
            }
            context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
            if (!context)
            {
                error = "Could not create PipeWire context";
                return false;
            }
        }
        struct pw_core *core = pw_context_connect_fd(context, fd, nullptr, 0);
        if (!core)
        {
            error = "Could not connect to the portal PipeWire remote";
            return false;
        }

        cores.push_back(core);
        for (std::size_t i = firstStream; i < streams.size(); ++i)
        {
            Stream *stream = streams[i].get();
            stream->pipewire = pw_stream_new(
                core, "RayNeo portal capture",
                pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
                                  PW_KEY_MEDIA_CATEGORY, "Capture",
                                  PW_KEY_MEDIA_ROLE, "Screen",
                                  nullptr));
            if (!stream->pipewire)
            {
                error = "Could not create a PipeWire capture stream";
                return false;
            }
            static const struct pw_stream_events events = {
                PW_VERSION_STREAM_EVENTS,
                .state_changed = onStateChanged,
                .param_changed = onParamChanged,
                .process = onProcess,
            };
            pw_stream_add_listener(stream->pipewire, &stream->listener, &events, stream);

            uint8_t buffer[1024];
            struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
            struct spa_rectangle preferred = SPA_RECTANGLE(1920, 1080);
            struct spa_rectangle minimum = SPA_RECTANGLE(1, 1);
            struct spa_rectangle maximum = SPA_RECTANGLE(8192, 8192);
            const struct spa_pod *params[1];
            params[0] = static_cast<const struct spa_pod *>(spa_pod_builder_add_object(
                &builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
                SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
                SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
                SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(4,
                    SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA,
                    SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA),
                SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(
                    &preferred, &minimum, &maximum)));
            if (pw_stream_connect(stream->pipewire, PW_DIRECTION_INPUT, stream->frame.nodeId,
                                  static_cast<enum pw_stream_flags>(
                                      PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
                                  params, 1) < 0)
            {
                error = "Could not connect a PipeWire capture stream";
                return false;
            }
        }
        if (startLoop && !loopStarted && pw_thread_loop_start(loop) < 0)
        {
            error = "Could not start PipeWire capture loop";
            return false;
        }
        if (startLoop)
            loopStarted = true;
        return true;
    }

    void stop()
    {
        if (loop)
            pw_thread_loop_stop(loop);
        for (const auto &entry : streams)
        {
            if (entry->pipewire)
            {
                pw_stream_destroy(entry->pipewire);
                entry->pipewire = nullptr;
            }
        }
        for (struct pw_core *core : cores)
        {
            if (core)
                pw_core_disconnect(core);
        }
        if (context)
        {
            pw_context_destroy(context);
            context = nullptr;
        }
        if (loop)
        {
            pw_thread_loop_destroy(loop);
            loop = nullptr;
        }
        if (connection)
        {
            for (const std::string &sessionPath : sessionPaths)
                g_dbus_connection_call(connection, kPortalBus, sessionPath.c_str(), kSession,
                                       "Close", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE,
                                       -1, nullptr, nullptr, nullptr);
        }
        if (connection)
        {
            g_object_unref(connection);
            connection = nullptr;
        }
        if (pipewireInitialized)
        {
            pw_deinit();
            pipewireInitialized = false;
        }
        streams.clear();
        cores.clear();
        sessionPaths.clear();
        pointerSessionPath.clear();
        loopStarted = false;
    }
};

PortalCapture::PortalCapture() : impl_(new Impl) {}

PortalCapture::~PortalCapture()
{
    stop();
    delete impl_;
}

bool PortalCapture::start(std::string &error)
{
    stop();
    GError *gerror = nullptr;
    impl_->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
    if (!impl_->connection)
    {
        error = gerror ? gerror->message : "Could not connect to the KDE session bus";
        if (gerror)
            g_error_free(gerror);
        return false;
    }

    auto addSingleStream = [&](GVariant *results, uint32_t expectedType, std::size_t &first) {
        first = impl_->streams.size();
        GVariant *streams = g_variant_lookup_value(results, "streams", G_VARIANT_TYPE("a(ua{sv})"));
        if (!streams)
        {
            error = "KDE portal did not return a video stream";
            return false;
        }
        GVariantIter iterator;
        g_variant_iter_init(&iterator, streams);
        guint32 node = 0;
        GVariant *properties = nullptr;
        while (g_variant_iter_next(&iterator, "(u@a{sv})", &node, &properties))
        {
            auto stream = std::make_unique<Impl::Stream>();
            stream->frame.nodeId = node;
            stream->frame.sourceType = resultUint(properties, "source_type", 0);
            GVariant *size = g_variant_lookup_value(properties, "size", G_VARIANT_TYPE("(ii)"));
            if (size)
            {
                g_variant_get(size, "(ii)", &stream->frame.logicalWidth, &stream->frame.logicalHeight);
                g_variant_unref(size);
            }
            impl_->streams.push_back(std::move(stream));
            g_variant_unref(properties);
        }
        g_variant_unref(streams);
        if (impl_->streams.size() != first + 1)
        {
            error = "KDE portal returned an unexpected number of video streams";
            return false;
        }
        if (impl_->streams[first]->frame.sourceType != expectedType)
        {
            error = expectedType == kMonitorSource ?
                "select the Samsung monitor" : "select the Chrome window";
            return false;
        }
        return true;
    };
    auto openRemote = [&](const std::string &sessionPath, int &fd) {
        GUnixFDList *fdList = nullptr;
        GVariant *reply = g_dbus_connection_call_with_unix_fd_list_sync(
            impl_->connection, kPortalBus, kPortalPath, kScreenCast, "OpenPipeWireRemote",
            g_variant_new("(o@a{sv})", sessionPath.c_str(), emptyOptions()),
            G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &fdList,
            nullptr, &gerror);
        if (!reply)
        {
            error = gerror ? gerror->message : "Could not open the portal PipeWire remote";
            if (gerror) g_error_free(gerror);
            return false;
        }
        gint index = -1;
        g_variant_get(reply, "(h)", &index);
        g_variant_unref(reply);
        fd = g_unix_fd_list_get(fdList, index, &gerror);
        g_object_unref(fdList);
        if (fd < 0)
        {
            error = gerror ? gerror->message : "KDE portal returned an invalid PipeWire file descriptor";
            if (gerror) g_error_free(gerror);
            return false;
        }
        return true;
    };

    RequestResult monitorCreated;
    if (!requestAndWait(impl_->connection, kRemoteDesktop, "CreateSession",
                        g_variant_new("(@a{sv})", sessionOptions(token("monitor_create"), token("monitor"))),
                        monitorCreated, error))
        return false;
    impl_->pointerSessionPath = resultObjectPath(monitorCreated.results, "session_handle");
    g_variant_unref(monitorCreated.results);
    if (impl_->pointerSessionPath.empty()) { error = "KDE portal did not create a monitor session"; return false; }
    impl_->sessionPaths.push_back(impl_->pointerSessionPath);
    RequestResult devices;
    if (!requestAndWait(impl_->connection, kRemoteDesktop, "SelectDevices",
                        g_variant_new("(o@a{sv})", impl_->pointerSessionPath.c_str(),
                                      pointerOptions(token("devices"))), devices, error)) return false;
    g_variant_unref(devices.results);
    RequestResult monitorSources;
    if (!requestAndWait(impl_->connection, kScreenCast, "SelectSources",
                        g_variant_new("(o@a{sv})", impl_->pointerSessionPath.c_str(),
                                      sourceOptions(token("monitor_sources"), kMonitorSource,
                                                    kCursorModeEmbedded)),
                        monitorSources, error)) return false;
    g_variant_unref(monitorSources.results);
    std::printf("KDE portal: opening Samsung capture; KDE may restore its permission without a dialog\n");
    RequestResult monitorStarted;
    if (!requestAndWait(impl_->connection, kRemoteDesktop, "Start",
                        g_variant_new("(os@a{sv})", impl_->pointerSessionPath.c_str(), "", emptyOptions()),
                        monitorStarted, error)) return false;
    if ((resultUint(monitorStarted.results, "devices", 0) & kPointerDevice) == 0)
    {
        g_variant_unref(monitorStarted.results);
        error = "KDE portal did not grant pointer control";
        return false;
    }
    std::size_t monitorFirst = 0;
    const bool monitorOk = addSingleStream(monitorStarted.results, kMonitorSource, monitorFirst);
    g_variant_unref(monitorStarted.results);
    if (!monitorOk) return false;
    int monitorFd = -1;
    if (!openRemote(impl_->pointerSessionPath, monitorFd) ||
        !impl_->startPipeWire(monitorFd, monitorFirst, false, error)) return false;

    RequestResult windowCreated;
    if (!requestAndWait(impl_->connection, kScreenCast, "CreateSession",
                        g_variant_new("(@a{sv})", sessionOptions(token("window_create"), token("window"))),
                        windowCreated, error))
        return false;
    const std::string windowSession = resultObjectPath(windowCreated.results, "session_handle");
    g_variant_unref(windowCreated.results);
    if (windowSession.empty()) { error = "KDE portal did not create a Chrome capture session"; return false; }
    impl_->sessionPaths.push_back(windowSession);
    RequestResult windowSources;
    if (!requestAndWait(impl_->connection, kScreenCast, "SelectSources",
                        g_variant_new("(o@a{sv})", windowSession.c_str(),
                                      sourceOptions(token("window_sources"), kWindowSource,
                                                    kCursorModeHidden)),
                        windowSources, error)) return false;
    g_variant_unref(windowSources.results);
    std::printf("KDE portal: opening Chrome capture\n");
    RequestResult windowStarted;
    if (!requestAndWait(impl_->connection, kScreenCast, "Start",
                        g_variant_new("(os@a{sv})", windowSession.c_str(), "", emptyOptions()),
                        windowStarted, error)) return false;
    std::size_t windowFirst = 0;
    const bool windowOk = addSingleStream(windowStarted.results, kWindowSource, windowFirst);
    g_variant_unref(windowStarted.results);
    if (!windowOk) return false;
    int windowFd = -1;
    if (!openRemote(windowSession, windowFd) ||
        !impl_->startPipeWire(windowFd, windowFirst, true, error)) return false;
    std::printf("KDE portal live capture started with %zu stream(s)\n", impl_->streams.size());
    for (std::size_t i = 0; i < impl_->streams.size(); ++i)
    {
        const PortalFrame &frame = impl_->streams[i]->frame;
        const char *kind = frame.sourceType == kMonitorSource ? "monitor" :
                           frame.sourceType == kWindowSource ? "window" : "unknown";
        std::printf("  portal stream %zu: %s (%dx%d)\n", i, kind,
                    frame.logicalWidth, frame.logicalHeight);
    }
    return true;
}

void PortalCapture::stop()
{
    impl_->stop();
}

bool PortalCapture::takeFrame(std::size_t index, PortalFrame &frame)
{
    if (index >= impl_->streams.size())
        return false;
    Impl::Stream &stream = *impl_->streams[index];
    std::lock_guard<std::mutex> lock(stream.mutex);
    if (!stream.ready)
        return false;
    frame = stream.frame;
    stream.ready = false;
    return true;
}

std::size_t PortalCapture::streamCount() const
{
    return impl_->streams.size();
}

uint32_t PortalCapture::sourceType(std::size_t index) const
{
    return index < impl_->streams.size() ? impl_->streams[index]->frame.sourceType : 0;
}

bool PortalCapture::isVirtualOutput(std::size_t index) const
{
    return index < impl_->streams.size() && impl_->streams[index]->frame.virtualOutput;
}

void PortalCapture::movePointerAbsolute(std::size_t index, double x, double y)
{
    if (!impl_->connection || impl_->pointerSessionPath.empty() || index >= impl_->streams.size())
        return;
    const uint32_t node = impl_->streams[index]->frame.nodeId;
    g_dbus_connection_call(impl_->connection, kPortalBus, kPortalPath, kRemoteDesktop,
                           "NotifyPointerMotionAbsolute",
                           g_variant_new("(o@a{sv}udd)", impl_->pointerSessionPath.c_str(),
                                         emptyOptions(), node, x, y),
                           nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
}

void PortalCapture::pointerButton(int button, bool pressed)
{
    if (!impl_->connection || impl_->pointerSessionPath.empty())
        return;
    g_dbus_connection_call(impl_->connection, kPortalBus, kPortalPath, kRemoteDesktop,
                           "NotifyPointerButton",
                           g_variant_new("(o@a{sv}iu)", impl_->pointerSessionPath.c_str(),
                                         emptyOptions(), button, pressed ? 1u : 0u),
                           nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
}
