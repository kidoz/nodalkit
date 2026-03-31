/// @file wayland_backend.cpp
/// @brief Wayland platform backend implementation.

#include "wayland_backend.h"

#include "wayland_input.h"
#include "wayland_portal_helpers.h"
#include "wayland_surface.h"
#include "xdg-shell-client-protocol.h"

#include <cstdlib>
#include <cstring>
#include <gio/gio.h>
#include <mutex>
#include <nk/foundation/logging.h>
#include <nk/runtime/event_loop.h>
#include <optional>
#include <poll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <wayland-client.h>

namespace nk {

namespace {

constexpr const char* PortalBusName = "org.freedesktop.portal.Desktop";
constexpr const char* PortalObjectPath = "/org/freedesktop/portal/desktop";
constexpr const char* PortalFileChooserInterface = "org.freedesktop.portal.FileChooser";
constexpr const char* PortalRequestInterface = "org.freedesktop.portal.Request";
constexpr const char* DbusBusName = "org.freedesktop.DBus";
constexpr const char* DbusObjectPath = "/org/freedesktop/DBus";
constexpr const char* DbusInterface = "org.freedesktop.DBus";

struct PortalRequestState {
    GMainLoop* loop = nullptr;
    GVariant* results = nullptr;
    uint32_t response = 2;
    bool completed = false;
};

std::string make_portal_handle_token() {
    return "nk_" + std::to_string(g_random_int());
}

bool session_bus_name_has_owner(GDBusConnection* connection, const char* bus_name) {
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(connection,
                                                  DbusBusName,
                                                  DbusObjectPath,
                                                  DbusInterface,
                                                  "NameHasOwner",
                                                  g_variant_new("(s)", bus_name),
                                                  G_VARIANT_TYPE("(b)"),
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  -1,
                                                  nullptr,
                                                  &error);
    if (reply == nullptr) {
        if (error != nullptr) {
            NK_LOG_ERROR("Wayland", error->message);
            g_error_free(error);
        }
        return false;
    }

    gboolean has_owner = FALSE;
    g_variant_get(reply, "(b)", &has_owner);
    g_variant_unref(reply);
    return has_owner == TRUE;
}

GDBusConnection* portal_session_bus_connection() {
    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (connection == nullptr && error != nullptr) {
        NK_LOG_ERROR("Wayland", error->message);
        g_error_free(error);
    }
    return connection;
}

std::optional<std::pair<guint32, std::string>> portal_filter_value(std::string_view filter) {
    if (auto entry = detail::portal_filter_value(filter)) {
        return std::pair<guint32, std::string>{static_cast<guint32>(entry->kind), entry->value};
    }
    return std::nullopt;
}

std::optional<std::string> first_path_from_portal_results(GVariant* results) {
    if (results == nullptr) {
        return std::nullopt;
    }

    GVariant* uris = g_variant_lookup_value(results, "uris", G_VARIANT_TYPE("as"));
    if (uris == nullptr) {
        return std::nullopt;
    }

    GVariantIter iter;
    g_variant_iter_init(&iter, uris);
    const char* uri = nullptr;
    if (!g_variant_iter_next(&iter, "&s", &uri) || uri == nullptr) {
        g_variant_unref(uris);
        return std::nullopt;
    }

    GError* error = nullptr;
    char* path = g_filename_from_uri(uri, nullptr, &error);
    g_variant_unref(uris);

    if (path == nullptr) {
        if (error != nullptr) {
            NK_LOG_ERROR("Wayland", error->message);
            g_error_free(error);
        }
        return std::nullopt;
    }

    std::string converted_path = path;
    g_free(path);
    return converted_path;
}

void on_portal_request_response(GDBusConnection* /*connection*/,
                                const gchar* /*sender_name*/,
                                const gchar* /*object_path*/,
                                const gchar* /*interface_name*/,
                                const gchar* /*signal_name*/,
                                GVariant* parameters,
                                gpointer user_data) {
    auto* state = static_cast<PortalRequestState*>(user_data);
    if (state->completed) {
        return;
    }

    g_variant_get(parameters, "(u@a{sv})", &state->response, &state->results);
    state->completed = true;
    if (state->loop != nullptr) {
        g_main_loop_quit(state->loop);
    }
}

// ---------------------------------------------------------------------------
// XDG WM Base listener — respond to compositor pings
// ---------------------------------------------------------------------------

static void wm_base_ping(void* /*data*/, struct xdg_wm_base* wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

static constexpr struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

// ---------------------------------------------------------------------------
// Registry listener — bind required Wayland globals
// ---------------------------------------------------------------------------

static void registry_global(void* data,
                            struct wl_registry* registry,
                            uint32_t name,
                            const char* interface,
                            uint32_t version);
static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name);

static constexpr struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// ---------------------------------------------------------------------------
// WaylandBackend::Impl
// ---------------------------------------------------------------------------

struct WaylandBackend::Impl {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    struct xdg_wm_base* wm_base = nullptr;
    wl_seat* seat = nullptr;

    std::unique_ptr<WaylandInput> input;

    int wake_fd = -1;
    EventLoop* current_loop = nullptr;
    int exit_code = 0;
    bool quit_requested = false;

    std::unordered_map<wl_surface*, WaylandSurface*> surfaces;

    mutable std::mutex system_preferences_mutex;
    SystemPreferencesObserver system_preferences_observer;
    std::thread system_preferences_thread;
    GMainContext* system_preferences_context = nullptr;
    GMainLoop* system_preferences_loop = nullptr;
    GSettings* interface_settings = nullptr;
    GSettings* a11y_interface_settings = nullptr;
    bool system_preferences_stop_requested = false;
};

namespace {

DesktopEnvironment detect_desktop_environment(const char* value) {
    if (value == nullptr) {
        return DesktopEnvironment::Unknown;
    }

    auto desktop = std::string_view(value);
    if (desktop.find("GNOME") != std::string_view::npos ||
        desktop.find("gnome") != std::string_view::npos) {
        return DesktopEnvironment::Gnome;
    }
    if (desktop.find("KDE") != std::string_view::npos ||
        desktop.find("Plasma") != std::string_view::npos ||
        desktop.find("plasma") != std::string_view::npos) {
        return DesktopEnvironment::KDE;
    }
    return DesktopEnvironment::Other;
}

DesktopEnvironment detect_linux_desktop_environment() {
    auto desktop = detect_desktop_environment(std::getenv("XDG_CURRENT_DESKTOP"));
    if (desktop != DesktopEnvironment::Unknown) {
        return desktop;
    }
    return detect_desktop_environment(std::getenv("DESKTOP_SESSION"));
}

bool looks_like_dark_theme(std::string_view theme_name) {
    return theme_name.find(":dark") != std::string_view::npos ||
           theme_name.find("dark") != std::string_view::npos ||
           theme_name.find("Dark") != std::string_view::npos;
}

bool has_gsettings_schema(const char* schema_name) {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    if (source == nullptr) {
        return false;
    }

    GSettingsSchema* schema = g_settings_schema_source_lookup(source, schema_name, TRUE);
    if (schema == nullptr) {
        return false;
    }

    g_settings_schema_unref(schema);
    return true;
}

GSettings* create_settings_for_schema(const char* schema_name) {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    if (source == nullptr) {
        return nullptr;
    }

    GSettingsSchema* schema = g_settings_schema_source_lookup(source, schema_name, TRUE);
    if (schema == nullptr) {
        return nullptr;
    }

    GSettings* settings = g_settings_new_full(schema, nullptr, nullptr);
    g_settings_schema_unref(schema);
    return settings;
}

std::optional<Color> gnome_accent_color(std::string_view accent_name) {
    if (accent_name == "blue") {
        return Color::from_rgb(53, 132, 228);
    }
    if (accent_name == "teal") {
        return Color::from_rgb(33, 144, 164);
    }
    if (accent_name == "green") {
        return Color::from_rgb(46, 194, 126);
    }
    if (accent_name == "yellow") {
        return Color::from_rgb(245, 194, 17);
    }
    if (accent_name == "orange") {
        return Color::from_rgb(255, 120, 0);
    }
    if (accent_name == "red") {
        return Color::from_rgb(230, 45, 66);
    }
    if (accent_name == "pink") {
        return Color::from_rgb(213, 97, 153);
    }
    if (accent_name == "purple") {
        return Color::from_rgb(145, 65, 172);
    }
    if (accent_name == "slate") {
        return Color::from_rgb(106, 115, 125);
    }
    return std::nullopt;
}

SystemPreferences linux_preferences_from_gsettings(GSettings* interface_settings,
                                                   GSettings* a11y_settings,
                                                   DesktopEnvironment desktop_environment) {
    SystemPreferences preferences;
    preferences.platform_family = PlatformFamily::Linux;
    preferences.desktop_environment = desktop_environment;
    preferences.color_scheme = ColorScheme::Light;

    if (interface_settings != nullptr) {
        char* color_scheme = g_settings_get_string(interface_settings, "color-scheme");
        if (color_scheme != nullptr) {
            auto scheme = std::string_view(color_scheme);
            if (scheme == "prefer-dark") {
                preferences.color_scheme = ColorScheme::Dark;
            } else if (scheme == "prefer-light") {
                preferences.color_scheme = ColorScheme::Light;
            } else {
                char* theme_name = g_settings_get_string(interface_settings, "gtk-theme");
                if (theme_name != nullptr) {
                    preferences.color_scheme =
                        looks_like_dark_theme(theme_name) ? ColorScheme::Dark : ColorScheme::Light;
                    g_free(theme_name);
                }
            }
            g_free(color_scheme);
        }

        char* accent_name = g_settings_get_string(interface_settings, "accent-color");
        if (accent_name != nullptr) {
            preferences.accent_color = gnome_accent_color(accent_name);
            g_free(accent_name);
        }

        preferences.text_scale_factor =
            static_cast<float>(g_settings_get_double(interface_settings, "text-scaling-factor"));
        if (!g_settings_get_boolean(interface_settings, "enable-animations")) {
            preferences.motion = MotionPreference::Reduced;
        }
    }

    if (a11y_settings != nullptr && g_settings_get_boolean(a11y_settings, "high-contrast")) {
        preferences.contrast = ContrastPreference::High;
    }

    return preferences;
}

SystemPreferences fallback_linux_preferences() {
    SystemPreferences preferences;
    preferences.platform_family = PlatformFamily::Linux;
    preferences.color_scheme = ColorScheme::Light;
    preferences.desktop_environment = detect_linux_desktop_environment();

    if (auto* gtk_theme = std::getenv("GTK_THEME");
        gtk_theme != nullptr && looks_like_dark_theme(gtk_theme)) {
        preferences.color_scheme = ColorScheme::Dark;
    }

    return preferences;
}

SystemPreferences query_linux_preferences() {
    const auto desktop_environment = detect_linux_desktop_environment();
    if (desktop_environment != DesktopEnvironment::Gnome ||
        !has_gsettings_schema("org.gnome.desktop.interface")) {
        return fallback_linux_preferences();
    }

    GSettings* interface_settings = create_settings_for_schema("org.gnome.desktop.interface");
    GSettings* a11y_settings = create_settings_for_schema("org.gnome.desktop.a11y.interface");

    auto preferences =
        linux_preferences_from_gsettings(interface_settings, a11y_settings, desktop_environment);

    if (interface_settings != nullptr) {
        g_object_unref(interface_settings);
    }
    if (a11y_settings != nullptr) {
        g_object_unref(a11y_settings);
    }

    return preferences;
}

SystemPreferences observed_linux_preferences(const WaylandBackend::Impl& impl) {
    return linux_preferences_from_gsettings(
        impl.interface_settings, impl.a11y_interface_settings, detect_linux_desktop_environment());
}

void emit_linux_system_preferences_change(WaylandBackend::Impl& impl) {
    SystemPreferencesObserver observer;
    {
        std::lock_guard lock(impl.system_preferences_mutex);
        observer = impl.system_preferences_observer;
    }

    if (observer) {
        observer(observed_linux_preferences(impl));
    }
}

void on_interface_setting_changed(GSettings* /*settings*/, gchar* /*key*/, gpointer user_data) {
    emit_linux_system_preferences_change(*static_cast<WaylandBackend::Impl*>(user_data));
}

void on_a11y_setting_changed(GSettings* /*settings*/, gchar* /*key*/, gpointer user_data) {
    emit_linux_system_preferences_change(*static_cast<WaylandBackend::Impl*>(user_data));
}

gboolean quit_system_preferences_loop(gpointer user_data) {
    g_main_loop_quit(static_cast<GMainLoop*>(user_data));
    return G_SOURCE_REMOVE;
}

} // namespace

// ---------------------------------------------------------------------------
// Registry callbacks
// ---------------------------------------------------------------------------

static void registry_global(void* data,
                            struct wl_registry* registry,
                            uint32_t name,
                            const char* interface,
                            uint32_t version) {
    auto* impl = static_cast<WaylandBackend::Impl*>(data);

    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        impl->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        impl->shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, std::min(version, 1u)));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        impl->wm_base = static_cast<struct xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 1u)));
        xdg_wm_base_add_listener(impl->wm_base, &wm_base_listener, impl);
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        impl->seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
    }
}

static void
registry_global_remove(void* /*data*/, struct wl_registry* /*registry*/, uint32_t /*name*/) {
    // Global removal (e.g. unplugged monitor). No-op for now.
}

// ---------------------------------------------------------------------------
// WaylandBackend implementation
// ---------------------------------------------------------------------------

WaylandBackend::WaylandBackend() : impl_(std::make_unique<Impl>()) {}

WaylandBackend::~WaylandBackend() {
    shutdown();
}

Result<void> WaylandBackend::initialize() {
    impl_->display = wl_display_connect(nullptr);
    if (!impl_->display) {
        return Unexpected(std::string("Failed to connect to Wayland display"));
    }

    impl_->registry = wl_display_get_registry(impl_->display);
    wl_registry_add_listener(impl_->registry, &registry_listener, impl_.get());
    wl_display_roundtrip(impl_->display);

    if (!impl_->compositor || !impl_->shm || !impl_->wm_base) {
        return Unexpected(std::string("Required Wayland interfaces not available "
                                      "(compositor, shm, or xdg_wm_base missing)"));
    }

    // Create eventfd for cross-thread wakeup.
    impl_->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (impl_->wake_fd < 0) {
        return Unexpected(std::string("Failed to create eventfd"));
    }

    // Set up input handling if a seat was advertised.
    if (impl_->seat) {
        impl_->input = std::make_unique<WaylandInput>(*this, impl_->seat);
        // Process seat capabilities immediately.
        wl_display_roundtrip(impl_->display);
    }

    NK_LOG_INFO("Wayland", "Connected to Wayland display");
    return {};
}

void WaylandBackend::shutdown() {
    stop_system_preferences_observation();
    impl_->input.reset();

    if (impl_->wake_fd >= 0) {
        close(impl_->wake_fd);
        impl_->wake_fd = -1;
    }
    if (impl_->wm_base) {
        xdg_wm_base_destroy(impl_->wm_base);
        impl_->wm_base = nullptr;
    }
    if (impl_->seat) {
        wl_seat_destroy(impl_->seat);
        impl_->seat = nullptr;
    }
    if (impl_->shm) {
        wl_shm_destroy(impl_->shm);
        impl_->shm = nullptr;
    }
    if (impl_->compositor) {
        wl_compositor_destroy(impl_->compositor);
        impl_->compositor = nullptr;
    }
    if (impl_->registry) {
        wl_registry_destroy(impl_->registry);
        impl_->registry = nullptr;
    }
    if (impl_->display) {
        wl_display_disconnect(impl_->display);
        impl_->display = nullptr;
    }
}

std::unique_ptr<NativeSurface> WaylandBackend::create_surface(const WindowConfig& config,
                                                              Window& owner) {
    return std::make_unique<WaylandSurface>(*this, config, owner);
}

int WaylandBackend::run_event_loop(EventLoop& loop) {
    impl_->current_loop = &loop;
    impl_->quit_requested = false;
    impl_->exit_code = 0;

    const int display_fd = wl_display_get_fd(impl_->display);

    struct pollfd fds[2];
    fds[0].fd = display_fd;
    fds[0].events = POLLIN;
    fds[1].fd = impl_->wake_fd;
    fds[1].events = POLLIN;

    while (!impl_->quit_requested) {
        // Dispatch any events already decoded but not yet delivered.
        while (wl_display_prepare_read(impl_->display) != 0) {
            wl_display_dispatch_pending(impl_->display);
        }
        wl_display_flush(impl_->display);

        // Wait for new events (~120 Hz fallback timeout).
        const int ret = poll(fds, 2, 8);

        if (ret > 0 && (fds[0].revents & POLLIN)) {
            wl_display_read_events(impl_->display);
        } else {
            wl_display_cancel_read(impl_->display);
        }
        wl_display_dispatch_pending(impl_->display);

        // Drain the wakeup eventfd.
        if (ret > 0 && (fds[1].revents & POLLIN)) {
            uint64_t val = 0;
            (void)read(impl_->wake_fd, &val, sizeof(val));
        }

        // Drive the NK event loop (posted tasks, timers, idle callbacks).
        loop.poll();
    }

    impl_->current_loop = nullptr;
    return impl_->exit_code;
}

void WaylandBackend::wake_event_loop() {
    if (impl_->wake_fd >= 0) {
        const uint64_t val = 1;
        (void)write(impl_->wake_fd, &val, sizeof(val));
    }
}

void WaylandBackend::request_quit(int exit_code) {
    impl_->exit_code = exit_code;
    impl_->quit_requested = true;
    wake_event_loop();
}

bool WaylandBackend::supports_open_file_dialog() const {
    GDBusConnection* connection = portal_session_bus_connection();
    if (connection == nullptr) {
        return false;
    }

    const bool supported = session_bus_name_has_owner(connection, PortalBusName);
    g_object_unref(connection);
    return supported;
}

OpenFileDialogResult
WaylandBackend::show_open_file_dialog(std::string_view title,
                                      const std::vector<std::string>& filters) {
    GDBusConnection* connection = portal_session_bus_connection();
    if (connection == nullptr) {
        return Unexpected(FileDialogError::Failed);
    }

    if (!session_bus_name_has_owner(connection, PortalBusName)) {
        g_object_unref(connection);
        return Unexpected(FileDialogError::Unsupported);
    }

    const auto handle_token = make_portal_handle_token();
    auto request_path =
        detail::portal_request_path(g_dbus_connection_get_unique_name(connection), handle_token);

    PortalRequestState state;
    state.loop = g_main_loop_new(nullptr, FALSE);

    auto subscribe_to_request = [&](const std::string& path) {
        return g_dbus_connection_signal_subscribe(connection,
                                                  PortalBusName,
                                                  PortalRequestInterface,
                                                  "Response",
                                                  path.c_str(),
                                                  nullptr,
                                                  G_DBUS_SIGNAL_FLAGS_NONE,
                                                  on_portal_request_response,
                                                  &state,
                                                  nullptr);
    };

    guint subscription_id = subscribe_to_request(request_path);

    GVariantBuilder options_builder;
    g_variant_builder_init(&options_builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(
        &options_builder, "{sv}", "handle_token", g_variant_new_string(handle_token.c_str()));
    g_variant_builder_add(&options_builder, "{sv}", "modal", g_variant_new_boolean(TRUE));

    std::vector<std::pair<guint32, std::string>> filter_entries;
    filter_entries.reserve(filters.size());
    for (const auto& filter : filters) {
        if (auto entry = portal_filter_value(filter)) {
            filter_entries.push_back(std::move(*entry));
        }
    }

    if (!filter_entries.empty()) {
        GVariantBuilder filter_entry_builder;
        g_variant_builder_init(&filter_entry_builder, G_VARIANT_TYPE("a(us)"));
        for (const auto& [kind, value] : filter_entries) {
            g_variant_builder_add(&filter_entry_builder, "(us)", kind, value.c_str());
        }

        GVariantBuilder filters_builder;
        g_variant_builder_init(&filters_builder, G_VARIANT_TYPE("a(sa(us))"));
        g_variant_builder_add(&filters_builder,
                              "(s@a(us))",
                              "Supported files",
                              g_variant_builder_end(&filter_entry_builder));

        g_variant_builder_add(
            &options_builder, "{sv}", "filters", g_variant_builder_end(&filters_builder));
    }

    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        connection,
        PortalBusName,
        PortalObjectPath,
        PortalFileChooserInterface,
        "OpenFile",
        g_variant_new("(ssa{sv})", "", std::string(title).c_str(), &options_builder),
        G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error);
    if (reply == nullptr) {
        if (error != nullptr) {
            NK_LOG_ERROR("Wayland", error->message);
            g_error_free(error);
        }
        g_dbus_connection_signal_unsubscribe(connection, subscription_id);
        g_main_loop_unref(state.loop);
        g_object_unref(connection);
        return Unexpected(FileDialogError::Failed);
    }

    const char* returned_request_path = nullptr;
    g_variant_get(reply, "(&o)", &returned_request_path);
    if (returned_request_path != nullptr && request_path != returned_request_path) {
        g_dbus_connection_signal_unsubscribe(connection, subscription_id);
        request_path = returned_request_path;
        subscription_id = subscribe_to_request(request_path);
    }
    g_variant_unref(reply);

    g_main_loop_run(state.loop);

    g_dbus_connection_signal_unsubscribe(connection, subscription_id);
    g_main_loop_unref(state.loop);
    g_object_unref(connection);

    auto result = detail::portal_result_from_response(
        state.response, first_path_from_portal_results(state.results));

    if (state.results != nullptr) {
        g_variant_unref(state.results);
    }
    return result;
}

SystemPreferences WaylandBackend::system_preferences() const {
    return query_linux_preferences();
}

bool WaylandBackend::supports_system_preferences_observation() const {
    return detect_linux_desktop_environment() == DesktopEnvironment::Gnome &&
           has_gsettings_schema("org.gnome.desktop.interface");
}

void WaylandBackend::start_system_preferences_observation(SystemPreferencesObserver observer) {
    stop_system_preferences_observation();
    if (!supports_system_preferences_observation()) {
        return;
    }

    {
        std::lock_guard lock(impl_->system_preferences_mutex);
        impl_->system_preferences_observer = std::move(observer);
        impl_->system_preferences_stop_requested = false;
    }

    impl_->system_preferences_thread = std::thread([impl = impl_.get()] {
        GMainContext* context = g_main_context_new();
        g_main_context_push_thread_default(context);
        GMainLoop* loop = g_main_loop_new(context, FALSE);
        GSettings* interface_settings = create_settings_for_schema("org.gnome.desktop.interface");
        GSettings* a11y_settings = create_settings_for_schema("org.gnome.desktop.a11y.interface");

        {
            std::lock_guard lock(impl->system_preferences_mutex);
            impl->system_preferences_context = context;
            impl->system_preferences_loop = loop;
            impl->interface_settings = interface_settings;
            impl->a11y_interface_settings = a11y_settings;
        }

        if (interface_settings != nullptr) {
            g_signal_connect(
                interface_settings, "changed", G_CALLBACK(on_interface_setting_changed), impl);
        }
        if (a11y_settings != nullptr) {
            g_signal_connect(a11y_settings, "changed", G_CALLBACK(on_a11y_setting_changed), impl);
        }

        bool stop_requested = false;
        {
            std::lock_guard lock(impl->system_preferences_mutex);
            stop_requested = impl->system_preferences_stop_requested;
        }

        if (!stop_requested && (interface_settings != nullptr || a11y_settings != nullptr)) {
            g_main_loop_run(loop);
        }

        if (interface_settings != nullptr) {
            g_signal_handlers_disconnect_by_data(interface_settings, impl);
            g_object_unref(interface_settings);
        }
        if (a11y_settings != nullptr) {
            g_signal_handlers_disconnect_by_data(a11y_settings, impl);
            g_object_unref(a11y_settings);
        }

        g_main_context_pop_thread_default(context);

        {
            std::lock_guard lock(impl->system_preferences_mutex);
            impl->interface_settings = nullptr;
            impl->a11y_interface_settings = nullptr;
            impl->system_preferences_loop = nullptr;
            impl->system_preferences_context = nullptr;
        }

        g_main_loop_unref(loop);
        g_main_context_unref(context);
    });
}

void WaylandBackend::stop_system_preferences_observation() {
    GMainContext* context = nullptr;
    GMainLoop* loop = nullptr;

    {
        std::lock_guard lock(impl_->system_preferences_mutex);
        context = impl_->system_preferences_context;
        loop = impl_->system_preferences_loop;
        impl_->system_preferences_observer = {};
        impl_->system_preferences_stop_requested = true;
    }

    if (context != nullptr && loop != nullptr) {
        g_main_context_invoke(context, quit_system_preferences_loop, loop);
    }

    if (impl_->system_preferences_thread.joinable()) {
        impl_->system_preferences_thread.join();
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

wl_display* WaylandBackend::display() const {
    return impl_->display;
}

wl_compositor* WaylandBackend::compositor() const {
    return impl_->compositor;
}

wl_shm* WaylandBackend::shm() const {
    return impl_->shm;
}

xdg_wm_base* WaylandBackend::wm_base() const {
    return impl_->wm_base;
}

void WaylandBackend::register_surface(wl_surface* wl_surf, WaylandSurface* surface) {
    impl_->surfaces[wl_surf] = surface;
}

void WaylandBackend::unregister_surface(wl_surface* wl_surf) {
    impl_->surfaces.erase(wl_surf);
}

WaylandSurface* WaylandBackend::find_surface(wl_surface* wl_surf) const {
    const auto it = impl_->surfaces.find(wl_surf);
    return (it != impl_->surfaces.end()) ? it->second : nullptr;
}

} // namespace nk
