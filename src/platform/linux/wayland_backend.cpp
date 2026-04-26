/// @file wayland_backend.cpp
/// @brief Wayland platform backend implementation.

#include "wayland_backend.h"

#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "primary-selection-unstable-v1-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wayland_input.h"
#include "wayland_portal_helpers.h"
#include "wayland_surface.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <gio/gio.h>
#include <mutex>
#include <nk/accessibility/accessible.h>
#include <nk/accessibility/atspi_bridge.h>
#include <nk/foundation/logging.h>
#include <nk/runtime/event_loop.h>
#include <nk/ui_core/widget.h>
#include <optional>
#include <poll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <wayland-client.h>

namespace nk {

namespace {

constexpr const char* PortalBusName = "org.freedesktop.portal.Desktop";
constexpr const char* PortalObjectPath = "/org/freedesktop/portal/desktop";
constexpr const char* PortalFileChooserInterface = "org.freedesktop.portal.FileChooser";
constexpr const char* PortalRequestInterface = "org.freedesktop.portal.Request";
constexpr const char* PortalSettingsInterface = "org.freedesktop.portal.Settings";
constexpr const char* PortalAppearanceNamespace = "org.freedesktop.appearance";
constexpr const char* DbusBusName = "org.freedesktop.DBus";
constexpr const char* DbusObjectPath = "/org/freedesktop/DBus";
constexpr const char* DbusInterface = "org.freedesktop.DBus";
constexpr const char* AtspiBusName = "org.a11y.Bus";
constexpr const char* AtspiBusObjectPath = "/org/a11y/bus";
constexpr const char* AtspiBusInterface = "org.a11y.Bus";
constexpr const char* AtspiObjectRootPath = "/org/a11y/atspi/accessible";
// AT-SPI clients (Orca, GTK's at-spi2-atk, Qt's QAccessible2) look for the per-application root
// accessible at the `/root` child of the subtree. Expose our Application node there so standard
// tools can find us without a custom handshake.
constexpr const char* AtspiApplicationRootPath = "/org/a11y/atspi/accessible/root";
constexpr const char* AtspiApplicationRootName = "root";
constexpr const char* AtspiAccessibleInterface = "org.a11y.atspi.Accessible";
constexpr const char* AtspiApplicationInterface = "org.a11y.atspi.Application";
constexpr const char* AtspiComponentInterface = "org.a11y.atspi.Component";
constexpr const char* AtspiActionInterface = "org.a11y.atspi.Action";
constexpr const char* AtspiTextInterface = "org.a11y.atspi.Text";

struct AtspiSnapshotState {
    AtspiAccessibleNode application;
    std::vector<AtspiAccessibleNode> nodes;
};

struct PortalRequestState {
    GMainLoop* loop = nullptr;
    GVariant* results = nullptr;
    uint32_t response = 2;
    bool completed = false;
};

GDBusNodeInfo* atspi_node_info() {
    static GDBusNodeInfo* node_info = []() {
        constexpr const char* xml = R"xml(
<node>
  <interface name="org.a11y.atspi.Accessible">
    <method name="GetChildren">
      <arg name="children" type="a(so)" direction="out"/>
    </method>
    <method name="GetRoleName">
      <arg name="role_name" type="s" direction="out"/>
    </method>
    <method name="GetLocalizedRoleName">
      <arg name="localized_role_name" type="s" direction="out"/>
    </method>
    <method name="GetState">
      <arg name="state" type="u" direction="out"/>
    </method>
    <method name="GetInterfaces">
      <arg name="interfaces" type="as" direction="out"/>
    </method>
    <property name="Name" type="s" access="read"/>
    <property name="Description" type="s" access="read"/>
    <property name="Parent" type="o" access="read"/>
    <property name="ChildCount" type="i" access="read"/>
  </interface>
  <interface name="org.a11y.atspi.Application">
    <method name="GetToolkitName">
      <arg name="toolkit_name" type="s" direction="out"/>
    </method>
    <method name="GetVersion">
      <arg name="version" type="s" direction="out"/>
    </method>
  </interface>
  <interface name="org.a11y.atspi.Component">
    <method name="GetExtents">
      <arg name="x" type="i" direction="out"/>
      <arg name="y" type="i" direction="out"/>
      <arg name="width" type="i" direction="out"/>
      <arg name="height" type="i" direction="out"/>
    </method>
  </interface>
  <interface name="org.a11y.atspi.Action">
    <method name="GetNActions">
      <arg name="count" type="i" direction="out"/>
    </method>
    <method name="GetActions">
      <arg name="actions" type="a(sss)" direction="out"/>
    </method>
    <method name="DoAction">
      <arg name="index" type="i" direction="in"/>
      <arg name="success" type="b" direction="out"/>
    </method>
  </interface>
  <interface name="org.a11y.atspi.Text">
    <method name="GetText">
      <arg name="start" type="i" direction="in"/>
      <arg name="end" type="i" direction="in"/>
      <arg name="text" type="s" direction="out"/>
    </method>
    <property name="CharacterCount" type="i" access="read"/>
  </interface>
</node>
)xml";
        GError* error = nullptr;
        GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(xml, &error);
        if (info == nullptr && error != nullptr) {
            NK_LOG_ERROR("WaylandA11y", error->message);
            g_error_free(error);
        }
        return info;
    }();
    return node_info;
}

GDBusInterfaceInfo* lookup_atspi_interface_info(const char* interface_name) {
    GDBusNodeInfo* info = atspi_node_info();
    if (info == nullptr) {
        return nullptr;
    }
    return g_dbus_node_info_lookup_interface(info, interface_name);
}

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

std::optional<std::string> atspi_bus_address(GDBusConnection* session_connection) {
    if (session_connection == nullptr ||
        !session_bus_name_has_owner(session_connection, AtspiBusName)) {
        return std::nullopt;
    }

    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(session_connection,
                                                  AtspiBusName,
                                                  AtspiBusObjectPath,
                                                  AtspiBusInterface,
                                                  "GetAddress",
                                                  nullptr,
                                                  G_VARIANT_TYPE("(s)"),
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  -1,
                                                  nullptr,
                                                  &error);
    if (reply == nullptr) {
        if (error != nullptr) {
            NK_LOG_ERROR("WaylandA11y", error->message);
            g_error_free(error);
        }
        return std::nullopt;
    }

    const char* address = nullptr;
    g_variant_get(reply, "(&s)", &address);
    std::optional<std::string> result;
    if (address != nullptr) {
        result = std::string(address);
    }
    g_variant_unref(reply);
    return result;
}

GDBusConnection* atspi_bus_connection() {
    GDBusConnection* session_connection = portal_session_bus_connection();
    if (session_connection == nullptr) {
        return nullptr;
    }

    const auto address = atspi_bus_address(session_connection);
    g_object_unref(session_connection);
    if (!address.has_value()) {
        return nullptr;
    }

    GError* error = nullptr;
    GDBusConnection* connection = g_dbus_connection_new_for_address_sync(
        address->c_str(),
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr,
        nullptr,
        &error);
    if (connection == nullptr && error != nullptr) {
        NK_LOG_ERROR("WaylandA11y", error->message);
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

// --- wl_output listener: track per-monitor scale for the HiDPI surface path ---

void output_geometry(void* /*data*/,
                     wl_output* /*output*/,
                     int32_t /*x*/,
                     int32_t /*y*/,
                     int32_t /*physical_width*/,
                     int32_t /*physical_height*/,
                     int32_t /*subpixel*/,
                     const char* /*make*/,
                     const char* /*model*/,
                     int32_t /*transform*/) {}

void output_mode(void* /*data*/,
                 wl_output* /*output*/,
                 uint32_t /*flags*/,
                 int32_t /*width*/,
                 int32_t /*height*/,
                 int32_t /*refresh*/) {}

void output_scale(void* data, wl_output* output, int32_t scale);
void output_done(void* data, wl_output* output);

void output_name(void* /*data*/, wl_output* /*output*/, const char* /*name*/) {}

void output_description(void* /*data*/, wl_output* /*output*/, const char* /*description*/) {}

constexpr struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

} // namespace

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
    wl_data_device_manager* data_device_manager = nullptr;
    zwp_text_input_manager_v3* text_input_manager = nullptr;
    zwp_primary_selection_device_manager_v1* primary_selection_manager = nullptr;
    wp_cursor_shape_manager_v1* cursor_shape_manager = nullptr;
    wp_fractional_scale_manager_v1* fractional_scale_manager = nullptr;
    wp_viewporter* viewporter = nullptr;
    std::unordered_map<wl_output*, int> output_scales;
    // Scales staged by a wl_output are latched on the wl_output.done event. We therefore keep a
    // side-map of "pending" values until the atomic done arrives, matching the wayland protocol.
    std::unordered_map<wl_output*, int> pending_output_scales;

    std::unique_ptr<WaylandInput> input;
    std::string clipboard_text_cache;
    std::string primary_selection_text_cache;

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

    mutable std::mutex accessibility_mutex;
    std::thread accessibility_thread;
    GMainContext* accessibility_context = nullptr;
    GMainLoop* accessibility_loop = nullptr;
    GDBusConnection* accessibility_connection = nullptr;
    guint accessibility_subtree_id = 0;
    // Direct object registrations keep GDBus from filtering out /accessible and /accessible/root
    // calls that the subtree dispatch path silently rejects on some GDBus versions. Keyed by
    // object path so we can diff against a freshly-built snapshot on widget-tree change.
    std::unordered_map<std::string, std::vector<guint>> accessibility_objects_by_path;
    // Cached AT-SPI snapshot built on the main thread (widgets aren't thread-safe) and read by
    // the a11y thread's method handlers. Protected by `accessibility_mutex`.
    AtspiSnapshotState cached_accessibility_snapshot;
    bool accessibility_stop_requested = false;
};

namespace {

void output_scale(void* data, wl_output* output, int32_t scale) {
    auto* impl = static_cast<WaylandBackend::Impl*>(data);
    if (scale <= 0) {
        scale = 1;
    }
    impl->pending_output_scales[output] = scale;
}

void output_done(void* data, wl_output* output) {
    auto* impl = static_cast<WaylandBackend::Impl*>(data);
    const auto pending_it = impl->pending_output_scales.find(output);
    if (pending_it == impl->pending_output_scales.end()) {
        // The compositor didn't advertise a scale before done (protocol implies default 1).
        impl->output_scales[output] = 1;
    } else {
        impl->output_scales[output] = pending_it->second;
        impl->pending_output_scales.erase(pending_it);
    }
    // Scale may have changed for every surface currently on this output. Let each surface
    // recompute.
    for (const auto& [wl_surf, surface] : impl->surfaces) {
        if (surface != nullptr) {
            surface->recompute_scale_factor();
        }
    }
}

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

// Calls org.freedesktop.portal.Settings.Read(namespace, key). The returned variant unwraps the
// inner `v` nested inside the `(v)` reply. Returns nullptr (with transferred ownership) when the
// key is unavailable or the call fails — callers should treat that as "no-preference" and leave
// the matching SystemPreferences field alone.
GVariant*
read_portal_setting(GDBusConnection* connection, const char* namespace_name, const char* key) {
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(connection,
                                                  PortalBusName,
                                                  PortalObjectPath,
                                                  PortalSettingsInterface,
                                                  "Read",
                                                  g_variant_new("(ss)", namespace_name, key),
                                                  G_VARIANT_TYPE("(v)"),
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  500,
                                                  nullptr,
                                                  &error);
    if (reply == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return nullptr;
    }
    GVariant* inner = nullptr;
    g_variant_get(reply, "(v)", &inner);
    g_variant_unref(reply);
    return inner;
}

// Decodes an org.freedesktop.appearance.accent-color variant. The portal spec publishes this as a
// (ddd) or (dddd) tuple of normalized sRGB doubles; be lenient about optional alpha.
std::optional<Color> parse_portal_accent_color(GVariant* value) {
    if (value == nullptr) {
        return std::nullopt;
    }
    const GVariantType* type = g_variant_get_type(value);
    double r = 0;
    double g = 0;
    double b = 0;
    double a = 1.0;
    if (g_variant_type_equal(type, G_VARIANT_TYPE("(ddd)"))) {
        g_variant_get(value, "(ddd)", &r, &g, &b);
    } else if (g_variant_type_equal(type, G_VARIANT_TYPE("(dddd)"))) {
        g_variant_get(value, "(dddd)", &r, &g, &b, &a);
    } else {
        return std::nullopt;
    }
    auto clamp_to_byte = [](double v) {
        v = std::max(0.0, std::min(1.0, v));
        return static_cast<uint8_t>(v * 255.0 + 0.5);
    };
    (void)a;
    return Color::from_rgb(clamp_to_byte(r), clamp_to_byte(g), clamp_to_byte(b));
}

// Overlays portal org.freedesktop.appearance values onto preferences already seeded by GSettings.
// Portal values "win" where reported; "no-preference" leaves the GSettings value intact.
void apply_portal_appearance_overrides(SystemPreferences& preferences,
                                       GDBusConnection* connection) {
    if (connection == nullptr) {
        return;
    }

    if (GVariant* value =
            read_portal_setting(connection, PortalAppearanceNamespace, "color-scheme");
        value != nullptr) {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) {
            const uint32_t scheme = g_variant_get_uint32(value);
            if (scheme == 1) {
                preferences.color_scheme = ColorScheme::Dark;
            } else if (scheme == 2) {
                preferences.color_scheme = ColorScheme::Light;
            }
            // 0 = no-preference: leave whatever GSettings/fallback set.
        }
        g_variant_unref(value);
    }

    if (GVariant* value = read_portal_setting(connection, PortalAppearanceNamespace, "contrast");
        value != nullptr) {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) {
            const uint32_t contrast = g_variant_get_uint32(value);
            if (contrast == 1) {
                preferences.contrast = ContrastPreference::High;
            } else if (contrast == 0) {
                preferences.contrast = ContrastPreference::Normal;
            }
        }
        g_variant_unref(value);
    }

    if (GVariant* value =
            read_portal_setting(connection, PortalAppearanceNamespace, "accent-color");
        value != nullptr) {
        if (auto color = parse_portal_accent_color(value)) {
            preferences.accent_color = *color;
        }
        g_variant_unref(value);
    }
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

    // Seed from GSettings when we have a GNOME schema — this path covers text-scale and
    // reduce-motion, which the portal's org.freedesktop.appearance namespace does not expose.
    // Otherwise start from the env-var fallback.
    SystemPreferences preferences;
    if (desktop_environment == DesktopEnvironment::Gnome &&
        has_gsettings_schema("org.gnome.desktop.interface")) {
        GSettings* interface_settings = create_settings_for_schema("org.gnome.desktop.interface");
        GSettings* a11y_settings = create_settings_for_schema("org.gnome.desktop.a11y.interface");
        preferences = linux_preferences_from_gsettings(
            interface_settings, a11y_settings, desktop_environment);
        if (interface_settings != nullptr) {
            g_object_unref(interface_settings);
        }
        if (a11y_settings != nullptr) {
            g_object_unref(a11y_settings);
        }
    } else {
        preferences = fallback_linux_preferences();
    }

    // Prefer portal values for color-scheme, contrast, and accent-color. Portal is desktop-agnostic
    // and reflects the desktop's declared cross-toolkit intent, while GSettings is GNOME-specific.
    if (GDBusConnection* portal = portal_session_bus_connection(); portal != nullptr) {
        if (session_bus_name_has_owner(portal, PortalBusName)) {
            apply_portal_appearance_overrides(preferences, portal);
        }
        g_object_unref(portal);
    }

    return preferences;
}

SystemPreferences observed_linux_preferences(const WaylandBackend::Impl& impl) {
    // Always re-run the full query so portal overlays are applied consistently; `impl` keeps the
    // live GSettings handles alive for the duration of observation, so the GSettings reads are
    // still cheap. The portal Read calls add a couple of sub-millisecond D-Bus roundtrips, which
    // is negligible at the cadence of system-preference changes.
    (void)impl;
    return query_linux_preferences();
}

// Forward declarations — defined later alongside the subtree dispatch vtable.
void atspi_method_call(GDBusConnection*,
                       const gchar*,
                       const gchar*,
                       const gchar*,
                       const gchar*,
                       GVariant*,
                       GDBusMethodInvocation*,
                       gpointer);
GVariant* atspi_get_property(
    GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GError**, gpointer);
AtspiSnapshotState build_atspi_snapshot_state(const WaylandBackend::Impl& impl);

// Returns a thread-safe copy of the cached accessibility snapshot. Built on the main thread via
// refresh_accessibility_snapshot; the a11y worker reads it via this helper under the mutex. The
// widget tree is not thread-safe, so the worker MUST NOT call build_atspi_snapshot_state itself.
AtspiSnapshotState copy_cached_accessibility_snapshot(WaylandBackend::Impl* impl) {
    std::lock_guard lock(impl->accessibility_mutex);
    return impl->cached_accessibility_snapshot;
}

// Registers explicit GDBus object handlers at `object_path` for all three AT-SPI interfaces we
// advertise on window/widget nodes. Returns the registration IDs so they can be unregistered on
// teardown or snapshot diff. Must run on the thread that owns `connection`.
std::vector<guint> register_atspi_object_at(GDBusConnection* connection,
                                            const std::string& object_path,
                                            WaylandBackend::Impl* impl) {
    if (g_variant_is_object_path(object_path.c_str()) == 0) {
        return {};
    }
    static const GDBusInterfaceVTable vtable = {
        .method_call = atspi_method_call,
        .get_property = atspi_get_property,
        .set_property = nullptr,
        .padding = {},
    };
    std::vector<guint> ids;
    static constexpr const char* kInterfaces[] = {
        AtspiAccessibleInterface,
        AtspiApplicationInterface,
        AtspiComponentInterface,
        AtspiActionInterface,
        AtspiTextInterface,
    };
    for (const char* iface : kInterfaces) {
        GDBusInterfaceInfo* info = lookup_atspi_interface_info(iface);
        if (info == nullptr) {
            continue;
        }
        GError* error = nullptr;
        const guint id = g_dbus_connection_register_object(
            connection, object_path.c_str(), info, &vtable, impl, nullptr, &error);
        if (id == 0U) {
            if (error != nullptr) {
                NK_LOG_ERROR("WaylandA11y", error->message);
                g_error_free(error);
            }
            continue;
        }
        ids.push_back(id);
    }
    return ids;
}

// Ensures that every node currently present in the snapshot has an explicit GDBus object
// registration. Called from the a11y thread (directly or via g_main_context_invoke) whenever
// the widget tree may have changed. Paths that dropped out of the snapshot are unregistered.
void sync_accessibility_objects(WaylandBackend::Impl* impl) {
    if (impl->accessibility_connection == nullptr) {
        return;
    }
    const auto snapshot = copy_cached_accessibility_snapshot(impl);

    std::unordered_set<std::string> live_paths;
    if (!snapshot.application.object_path.empty()) {
        live_paths.insert(snapshot.application.object_path);
    }
    for (const auto& node : snapshot.nodes) {
        if (!node.object_path.empty()) {
            live_paths.insert(node.object_path);
        }
    }

    // Unregister paths no longer present.
    for (auto it = impl->accessibility_objects_by_path.begin();
         it != impl->accessibility_objects_by_path.end();) {
        if (!live_paths.contains(it->first)) {
            for (const guint id : it->second) {
                g_dbus_connection_unregister_object(impl->accessibility_connection, id);
            }
            it = impl->accessibility_objects_by_path.erase(it);
        } else {
            ++it;
        }
    }

    // Register new paths.
    for (const auto& path : live_paths) {
        if (impl->accessibility_objects_by_path.contains(path)) {
            continue;
        }
        impl->accessibility_objects_by_path[path] =
            register_atspi_object_at(impl->accessibility_connection, path, impl);
    }
}

// Trampoline for g_main_context_invoke. Calls sync_accessibility_objects on the a11y thread.
gboolean sync_accessibility_objects_trampoline(gpointer user_data) {
    sync_accessibility_objects(static_cast<WaylandBackend::Impl*>(user_data));
    return G_SOURCE_REMOVE;
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

void on_portal_setting_changed(GDBusConnection* /*connection*/,
                               const gchar* /*sender_name*/,
                               const gchar* /*object_path*/,
                               const gchar* /*interface_name*/,
                               const gchar* /*signal_name*/,
                               GVariant* parameters,
                               gpointer user_data) {
    const gchar* namespace_name = nullptr;
    const gchar* key = nullptr;
    GVariant* value = nullptr;
    g_variant_get(parameters, "(&s&sv)", &namespace_name, &key, &value);
    // Only re-emit for namespaces we actually observe; the portal's SettingChanged signal is a
    // broadcast and fires for any key that changed (including ones unrelated to appearance).
    const bool appearance =
        namespace_name != nullptr && std::strcmp(namespace_name, PortalAppearanceNamespace) == 0;
    if (value != nullptr) {
        g_variant_unref(value);
    }
    if (appearance) {
        emit_linux_system_preferences_change(*static_cast<WaylandBackend::Impl*>(user_data));
    }
}

gboolean quit_system_preferences_loop(gpointer user_data) {
    g_main_loop_quit(static_cast<GMainLoop*>(user_data));
    return G_SOURCE_REMOVE;
}

AtspiSnapshotState build_atspi_snapshot_state(const WaylandBackend::Impl& impl) {
    AtspiSnapshotState snapshot;
    snapshot.application.object_path = AtspiApplicationRootPath;
    snapshot.application.object_name = AtspiApplicationRootName;
    snapshot.application.parent_path = "/";
    snapshot.application.role_name = "application";
    snapshot.application.name = "NodalKit";
    snapshot.application.state = AtspiStateBit::Enabled | AtspiStateBit::Sensitive |
                                 AtspiStateBit::Visible | AtspiStateBit::Showing;
    snapshot.application.interfaces = {AtspiAccessibleInterface, AtspiApplicationInterface};

    std::vector<WaylandSurface*> surfaces;
    surfaces.reserve(impl.surfaces.size());
    for (const auto& entry : impl.surfaces) {
        if (entry.second != nullptr) {
            surfaces.push_back(entry.second);
        }
    }
    std::sort(surfaces.begin(), surfaces.end());

    std::size_t window_index = 0;
    for (WaylandSurface* surface : surfaces) {
        const auto size = surface->size();
        std::string title = std::string(surface->owner().title());
        if (title.empty()) {
            title = "Window";
        }

        auto window_snapshot =
            build_atspi_window_snapshot(AtspiApplicationRootPath,
                                        "window" + std::to_string(window_index++),
                                        title,
                                        {0.0F, 0.0F, size.width, size.height},
                                        surface->owner().debug_tree());
        if (!window_snapshot.nodes.empty()) {
            snapshot.application.child_paths.push_back(window_snapshot.nodes.front().object_path);
        }
        snapshot.nodes.insert(snapshot.nodes.end(),
                              std::make_move_iterator(window_snapshot.nodes.begin()),
                              std::make_move_iterator(window_snapshot.nodes.end()));
    }

    if (!snapshot.application.child_paths.empty() && !snapshot.nodes.empty() &&
        !snapshot.nodes.front().name.empty()) {
        snapshot.application.name = snapshot.nodes.front().name;
    }

    return snapshot;
}

const AtspiAccessibleNode* find_atspi_node(const AtspiSnapshotState& snapshot,
                                           std::string_view object_path) {
    if (object_path == snapshot.application.object_path) {
        return &snapshot.application;
    }
    // NOTE: do NOT construct a temporary AtspiAccessibleSnapshot here — the previous version
    // passed `{snapshot.nodes}` which created a brace-initialized temporary; the returned
    // pointer dangled as soon as this function returned, and dereferencing it from a11y method
    // handlers produced garbage (and crashes on `GetChildren` with non-empty child_paths).
    for (const auto& node : snapshot.nodes) {
        if (node.object_path == object_path) {
            return &node;
        }
    }
    return nullptr;
}

Widget* resolve_widget_by_tree_path(Window& window, std::span<const std::size_t> tree_path) {
    Widget* current = window.child();
    if (current == nullptr) {
        return nullptr;
    }
    for (const auto index : tree_path) {
        const auto children = current->children();
        if (index >= children.size() || children[index] == nullptr) {
            return nullptr;
        }
        current = children[index].get();
    }
    return current;
}

Widget* find_live_atspi_widget(WaylandBackend::Impl& impl, std::string_view object_path) {
    std::vector<WaylandSurface*> surfaces;
    surfaces.reserve(impl.surfaces.size());
    for (const auto& entry : impl.surfaces) {
        if (entry.second != nullptr) {
            surfaces.push_back(entry.second);
        }
    }
    std::sort(surfaces.begin(), surfaces.end());

    std::size_t window_index = 0;
    for (WaylandSurface* surface : surfaces) {
        std::string title = std::string(surface->owner().title());
        if (title.empty()) {
            title = "Window";
        }
        const auto size = surface->size();
        const auto snapshot = build_atspi_window_snapshot(AtspiApplicationRootPath,
                                                          "window" + std::to_string(window_index++),
                                                          title,
                                                          {0.0F, 0.0F, size.width, size.height},
                                                          surface->owner().debug_tree());
        if (const auto* node = find_atspi_accessible_node(snapshot, object_path); node != nullptr) {
            return resolve_widget_by_tree_path(surface->owner(), node->tree_path);
        }
    }
    return nullptr;
}

std::string subtree_lookup_path(const gchar* object_path, const gchar* node) {
    if (node == nullptr || *node == '\0') {
        return std::string(object_path);
    }
    return std::string(object_path) + "/" + node;
}

gchar** atspi_subtree_enumerate(GDBusConnection* /*connection*/,
                                const gchar* /*sender*/,
                                const gchar* object_path,
                                gpointer user_data) {
    auto* impl = static_cast<WaylandBackend::Impl*>(user_data);
    if (std::string_view(object_path) != AtspiObjectRootPath) {
        return g_new0(gchar*, 1);
    }

    const auto snapshot = copy_cached_accessibility_snapshot(impl);
    // GDBus expects the names of direct children of the subtree root. The application node is
    // at "/root" under the subtree; the per-widget nodes live deeper, so they would not normally
    // be enumerated as direct children. We return the flat list (including the application and
    // all descendants) because the subtree is registered with G_DBUS_SUBTREE_FLAGS_NONE — any
    // path that isn't enumerated + introspected is rejected, and AT-SPI clients walk from `root`
    // deeper via object paths obtained from GetChildren responses. Enumerating everything flat
    // is a pragmatic way to keep GDBus from filtering out legitimate traversals.
    gchar** children = g_new0(gchar*, snapshot.nodes.size() + 2);
    std::size_t out = 0;
    if (!snapshot.application.object_name.empty()) {
        children[out++] = g_strdup(snapshot.application.object_name.c_str());
    }
    for (std::size_t index = 0; index < snapshot.nodes.size(); ++index) {
        children[out++] = g_strdup(snapshot.nodes[index].object_name.c_str());
    }
    return children;
}

GDBusInterfaceInfo** atspi_subtree_introspect(GDBusConnection* /*connection*/,
                                              const gchar* /*sender*/,
                                              const gchar* object_path,
                                              const gchar* node,
                                              gpointer user_data) {
    auto* impl = static_cast<WaylandBackend::Impl*>(user_data);
    const auto snapshot = copy_cached_accessibility_snapshot(impl);
    if (find_atspi_node(snapshot, subtree_lookup_path(object_path, node)) == nullptr) {
        return nullptr;
    }

    const auto* accessible_node = find_atspi_node(snapshot, subtree_lookup_path(object_path, node));
    if (accessible_node == nullptr) {
        return nullptr;
    }

    auto** interfaces = g_new0(GDBusInterfaceInfo*, accessible_node->interfaces.size() + 1);
    std::size_t interface_index = 0;
    for (const auto& interface_name : accessible_node->interfaces) {
        GDBusInterfaceInfo* info = lookup_atspi_interface_info(interface_name.c_str());
        if (info == nullptr) {
            continue;
        }
        interfaces[interface_index++] = g_dbus_interface_info_ref(info);
    }
    return interfaces;
}

void atspi_method_call(GDBusConnection* connection,
                       const gchar* /*sender*/,
                       const gchar* object_path,
                       const gchar* interface_name,
                       const gchar* method_name,
                       GVariant* parameters,
                       GDBusMethodInvocation* invocation,
                       gpointer user_data) {
    auto* impl = static_cast<WaylandBackend::Impl*>(user_data);
    const auto snapshot = copy_cached_accessibility_snapshot(impl);
    const auto* node = find_atspi_node(snapshot, object_path);
    if (node == nullptr) {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.a11y.atspi.Error.NotFound", "Accessible node not found");
        return;
    }

    if (std::string_view(interface_name) == AtspiAccessibleInterface) {
        if (std::string_view(method_name) == "GetRoleName" ||
            std::string_view(method_name) == "GetLocalizedRoleName") {
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(s)", node->role_name.c_str()));
            return;
        }
        if (std::string_view(method_name) == "GetState") {
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(u)", static_cast<uint32_t>(node->state)));
            return;
        }
        if (std::string_view(method_name) == "GetInterfaces") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
            for (const auto& iface : node->interfaces) {
                g_variant_builder_add(&builder, "s", iface.c_str());
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", &builder));
            return;
        }
        if (std::string_view(method_name) == "GetChildren") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("a(so)"));
            const char* bus_name = g_dbus_connection_get_unique_name(connection);
            for (const auto& child_path : node->child_paths) {
                // `g_variant_new("(so)", ...)` aborts the process on invalid object paths.
                if (g_variant_is_object_path(child_path.c_str()) == 0) {
                    continue;
                }
                g_variant_builder_add(&builder, "(so)", bus_name, child_path.c_str());
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(so))", &builder));
            return;
        }
    } else if (std::string_view(interface_name) == AtspiApplicationInterface) {
        if (std::string_view(method_name) == "GetToolkitName") {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", "NodalKit"));
            return;
        }
        if (std::string_view(method_name) == "GetVersion") {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", "0.1.0"));
            return;
        }
    } else if (std::string_view(interface_name) == AtspiComponentInterface) {
        if (std::string_view(method_name) == "GetExtents") {
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(iiii)",
                              static_cast<int>(node->bounds.x),
                              static_cast<int>(node->bounds.y),
                              static_cast<int>(node->bounds.width),
                              static_cast<int>(node->bounds.height)));
            return;
        }
    } else if (std::string_view(interface_name) == AtspiActionInterface) {
        if (std::string_view(method_name) == "GetNActions") {
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(i)", static_cast<int>(node->action_names.size())));
            return;
        }
        if (std::string_view(method_name) == "GetActions") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("a(sss)"));
            for (const auto& action_name : node->action_names) {
                g_variant_builder_add(&builder, "(sss)", action_name.c_str(), "", "");
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(sss))", &builder));
            return;
        }
        if (std::string_view(method_name) == "DoAction") {
            gint32 action_index = -1;
            g_variant_get(parameters, "(i)", &action_index);
            bool success = false;
            if (action_index >= 0 &&
                static_cast<std::size_t>(action_index) < node->action_names.size()) {
                if (auto* widget = find_live_atspi_widget(*impl, object_path);
                    widget != nullptr && widget->accessible() != nullptr) {
                    const auto action_name =
                        node->action_names[static_cast<std::size_t>(action_index)];
                    if (action_name == "activate") {
                        success = widget->accessible()->perform_action(AccessibleAction::Activate);
                    } else if (action_name == "focus") {
                        success = widget->accessible()->perform_action(AccessibleAction::Focus);
                    } else if (action_name == "toggle") {
                        success = widget->accessible()->perform_action(AccessibleAction::Toggle);
                    }
                }
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", success));
            return;
        }
    } else if (std::string_view(interface_name) == AtspiTextInterface) {
        if (std::string_view(method_name) == "GetText") {
            gint32 start = 0;
            gint32 end = -1;
            g_variant_get(parameters, "(ii)", &start, &end);
            const auto length = static_cast<gint32>(node->value.size());
            start = std::clamp(start, 0, length);
            end = end < 0 ? length : std::clamp(end, start, length);
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(s)",
                              node->value
                                  .substr(static_cast<std::size_t>(start),
                                          static_cast<std::size_t>(end - start))
                                  .c_str()));
            return;
        }
    }

    g_dbus_method_invocation_return_dbus_error(
        invocation, "org.a11y.atspi.Error.Unsupported", "Unsupported accessibility method");
}

GVariant* atspi_get_property(GDBusConnection* /*connection*/,
                             const gchar* /*sender*/,
                             const gchar* object_path,
                             const gchar* interface_name,
                             const gchar* property_name,
                             GError** error,
                             gpointer user_data) {
    auto* impl = static_cast<WaylandBackend::Impl*>(user_data);
    const auto snapshot = copy_cached_accessibility_snapshot(impl);
    const auto* node = find_atspi_node(snapshot, object_path);
    if (node == nullptr) {
        g_set_error(error,
                    G_DBUS_ERROR,
                    G_DBUS_ERROR_UNKNOWN_OBJECT,
                    "Unknown accessibility object '%s'",
                    object_path);
        return nullptr;
    }

    auto safe_utf8_variant = [](const std::string& value) -> GVariant* {
        if (g_utf8_validate(value.c_str(), static_cast<gssize>(value.size()), nullptr) == 0) {
            return g_variant_new_string("");
        }
        return g_variant_new_string(value.c_str());
    };
    if (std::string_view(interface_name) == AtspiAccessibleInterface) {
        if (std::string_view(property_name) == "Name") {
            return safe_utf8_variant(node->name);
        }
        if (std::string_view(property_name) == "Description") {
            return safe_utf8_variant(node->description);
        }
        if (std::string_view(property_name) == "Parent") {
            return g_variant_new_object_path(node->parent_path.empty() ? "/"
                                                                       : node->parent_path.c_str());
        }
        if (std::string_view(property_name) == "ChildCount") {
            return g_variant_new_int32(static_cast<int32_t>(node->child_paths.size()));
        }
    } else if (std::string_view(interface_name) == AtspiTextInterface) {
        if (std::string_view(property_name) == "CharacterCount") {
            return g_variant_new_int32(static_cast<int32_t>(node->value.size()));
        }
    }

    g_set_error(error,
                G_DBUS_ERROR,
                G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "Unknown accessibility property '%s'",
                property_name);
    return nullptr;
}

const GDBusInterfaceVTable* atspi_subtree_dispatch(GDBusConnection* /*connection*/,
                                                   const gchar* /*sender*/,
                                                   const gchar* /*object_path*/,
                                                   const gchar* interface_name,
                                                   const gchar* /*node*/,
                                                   gpointer* out_user_data,
                                                   gpointer user_data) {
    static const GDBusInterfaceVTable vtable = {
        .method_call = atspi_method_call,
        .get_property = atspi_get_property,
        .set_property = nullptr,
        .padding = {},
    };

    if (std::string_view(interface_name) != AtspiAccessibleInterface &&
        std::string_view(interface_name) != AtspiApplicationInterface &&
        std::string_view(interface_name) != AtspiComponentInterface &&
        std::string_view(interface_name) != AtspiActionInterface &&
        std::string_view(interface_name) != AtspiTextInterface) {
        return nullptr;
    }

    *out_user_data = user_data;
    return &vtable;
}

const GDBusSubtreeVTable atspi_subtree_vtable = {
    .enumerate = atspi_subtree_enumerate,
    .introspect = atspi_subtree_introspect,
    .dispatch = atspi_subtree_dispatch,
    .padding = {},
};

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
        // wl_compositor v6 enables wl_surface.preferred_buffer_scale / preferred_buffer_transform
        // events, which lets the compositor hint the ideal buffer scale even before the surface
        // enters any output. We still work on v4 compositors via the older enter/leave path.
        impl->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 6u)));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        impl->shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, std::min(version, 1u)));
    } else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        impl->data_device_manager = static_cast<wl_data_device_manager*>(wl_registry_bind(
            registry, name, &wl_data_device_manager_interface, std::min(version, 3u)));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        impl->wm_base = static_cast<struct xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 1u)));
        xdg_wm_base_add_listener(impl->wm_base, &wm_base_listener, impl);
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        impl->seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
    } else if (std::strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
        impl->text_input_manager = static_cast<zwp_text_input_manager_v3*>(wl_registry_bind(
            registry, name, &zwp_text_input_manager_v3_interface, std::min(version, 1u)));
    } else if (std::strcmp(interface, zwp_primary_selection_device_manager_v1_interface.name) ==
               0) {
        impl->primary_selection_manager = static_cast<zwp_primary_selection_device_manager_v1*>(
            wl_registry_bind(registry,
                             name,
                             &zwp_primary_selection_device_manager_v1_interface,
                             std::min(version, 1u)));
    } else if (std::strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        impl->cursor_shape_manager = static_cast<wp_cursor_shape_manager_v1*>(wl_registry_bind(
            registry, name, &wp_cursor_shape_manager_v1_interface, std::min(version, 1u)));
        NK_LOG_INFO("Wayland", "Bound wp_cursor_shape_manager_v1");
    } else if (std::strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        impl->fractional_scale_manager =
            static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(
                registry, name, &wp_fractional_scale_manager_v1_interface, std::min(version, 1u)));
        NK_LOG_INFO("Wayland", "Bound wp_fractional_scale_manager_v1");
    } else if (std::strcmp(interface, wp_viewporter_interface.name) == 0) {
        impl->viewporter = static_cast<wp_viewporter*>(
            wl_registry_bind(registry, name, &wp_viewporter_interface, std::min(version, 1u)));
        NK_LOG_INFO("Wayland", "Bound wp_viewporter");
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        // wl_output version 2 adds the `scale` event (the one we need); 3 adds release, 4 adds
        // name/description. We bind at up to 4 but gracefully handle older compositors.
        auto* output = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
        impl->output_scales[output] = 1;
        wl_output_add_listener(output, &output_listener, impl);
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
        if (!impl_->clipboard_text_cache.empty()) {
            impl_->input->set_clipboard_text(impl_->clipboard_text_cache);
        }
        if (!impl_->primary_selection_text_cache.empty()) {
            impl_->input->set_primary_selection_text(impl_->primary_selection_text_cache);
        }
        // Process seat capabilities immediately.
        wl_display_roundtrip(impl_->display);
    }

    start_accessibility_thread();

    NK_LOG_INFO("Wayland", "Connected to Wayland display");
    return {};
}

void WaylandBackend::start_accessibility_thread() {
    {
        std::lock_guard lock(impl_->accessibility_mutex);
        impl_->accessibility_stop_requested = false;
    }
    impl_->accessibility_thread = std::thread([impl = impl_.get()] {
        // Own GMainContext + thread-default so the a11y GDBusConnection dispatches all incoming
        // method calls (Accessible.GetRoleName, etc.) on this thread's mainloop. Without the
        // pushed thread-default, GDBus attaches the connection to the global NULL context, and
        // because the main thread never runs a GMainLoop, callbacks queue up but never fire.
        GMainContext* context = g_main_context_new();
        g_main_context_push_thread_default(context);
        GMainLoop* loop = g_main_loop_new(context, FALSE);

        GDBusConnection* connection = atspi_bus_connection();
        guint subtree_id = 0;
        if (connection != nullptr) {
            GError* error = nullptr;
            // Register the subtree at /org/a11y/atspi/accessible. Uses
            // DISPATCH_TO_UNENUMERATED_NODES so deeper widget paths (e.g.
            // /accessible/root/window0/0_0) reach the dispatch handler even though our enumerate
            // function couldn't represent them as single-segment names. Registration is done on
            // this thread so messages dispatch on its GMainContext — GDBus binds the subtree to
            // whatever thread-default context is current at registration time.
            subtree_id = g_dbus_connection_register_subtree(
                connection,
                AtspiObjectRootPath,
                &atspi_subtree_vtable,
                G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,
                impl,
                nullptr,
                &error);
            if (subtree_id == 0U) {
                if (error != nullptr) {
                    NK_LOG_ERROR("WaylandA11y", error->message);
                    g_error_free(error);
                }
                g_object_unref(connection);
                connection = nullptr;
            } else {
                NK_LOG_INFO(
                    "WaylandA11y",
                    "Registered AT-SPI accessibility subtree on the accessibility bus thread");
            }
        }

        bool stop_requested = false;
        {
            std::lock_guard lock(impl->accessibility_mutex);
            impl->accessibility_context = context;
            impl->accessibility_loop = loop;
            impl->accessibility_connection = connection;
            impl->accessibility_subtree_id = subtree_id;
            stop_requested = impl->accessibility_stop_requested;
        }

        if (!stop_requested && connection != nullptr) {
            // Register the application root and any surfaces that already exist.
            sync_accessibility_objects(impl);
            g_main_loop_run(loop);
        }

        // Teardown on this thread: unregister subtree and drop the connection so the final
        // unref happens on the same context that owns the GDBusConnection worker.
        if (connection != nullptr) {
            for (auto& [path, ids] : impl->accessibility_objects_by_path) {
                for (const guint id : ids) {
                    g_dbus_connection_unregister_object(connection, id);
                }
            }
        }
        impl->accessibility_objects_by_path.clear();
        if (connection != nullptr && subtree_id != 0U) {
            g_dbus_connection_unregister_subtree(connection, subtree_id);
        }
        if (connection != nullptr) {
            g_object_unref(connection);
        }

        g_main_context_pop_thread_default(context);

        {
            std::lock_guard lock(impl->accessibility_mutex);
            impl->accessibility_connection = nullptr;
            impl->accessibility_subtree_id = 0;
            impl->accessibility_loop = nullptr;
            impl->accessibility_context = nullptr;
        }

        g_main_loop_unref(loop);
        g_main_context_unref(context);
    });
}

void WaylandBackend::stop_accessibility_thread() {
    GMainContext* context = nullptr;
    GMainLoop* loop = nullptr;
    {
        std::lock_guard lock(impl_->accessibility_mutex);
        impl_->accessibility_stop_requested = true;
        context = impl_->accessibility_context;
        loop = impl_->accessibility_loop;
    }

    if (context != nullptr && loop != nullptr) {
        g_main_context_invoke(context, quit_system_preferences_loop, loop);
    }

    if (impl_->accessibility_thread.joinable()) {
        impl_->accessibility_thread.join();
    }
}

void WaylandBackend::shutdown() {
    stop_accessibility_thread();
    stop_system_preferences_observation();
    impl_->input.reset();
    impl_->clipboard_text_cache.clear();
    impl_->primary_selection_text_cache.clear();

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
    if (impl_->data_device_manager) {
        wl_data_device_manager_destroy(impl_->data_device_manager);
        impl_->data_device_manager = nullptr;
    }
    if (impl_->primary_selection_manager) {
        zwp_primary_selection_device_manager_v1_destroy(impl_->primary_selection_manager);
        impl_->primary_selection_manager = nullptr;
    }
    if (impl_->cursor_shape_manager) {
        wp_cursor_shape_manager_v1_destroy(impl_->cursor_shape_manager);
        impl_->cursor_shape_manager = nullptr;
    }
    if (impl_->fractional_scale_manager) {
        wp_fractional_scale_manager_v1_destroy(impl_->fractional_scale_manager);
        impl_->fractional_scale_manager = nullptr;
    }
    if (impl_->viewporter) {
        wp_viewporter_destroy(impl_->viewporter);
        impl_->viewporter = nullptr;
    }
    for (auto& [output, scale] : impl_->output_scales) {
        if (output != nullptr) {
            const uint32_t output_version =
                wl_proxy_get_version(reinterpret_cast<wl_proxy*>(output));
            if (output_version >= 3u) {
                wl_output_release(output);
            } else {
                wl_output_destroy(output);
            }
        }
    }
    impl_->output_scales.clear();
    impl_->pending_output_scales.clear();
    if (impl_->text_input_manager) {
        zwp_text_input_manager_v3_destroy(impl_->text_input_manager);
        impl_->text_input_manager = nullptr;
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

bool WaylandBackend::supports_clipboard_text() const {
    return impl_->data_device_manager != nullptr;
}

std::string WaylandBackend::clipboard_text() const {
    if (impl_->input != nullptr) {
        return impl_->input->clipboard_text();
    }
    return impl_->clipboard_text_cache;
}

void WaylandBackend::set_clipboard_text(std::string_view text) {
    impl_->clipboard_text_cache = std::string(text);
    if (impl_->input != nullptr) {
        impl_->input->set_clipboard_text(impl_->clipboard_text_cache);
    }
}

bool WaylandBackend::supports_primary_selection_text() const {
    return impl_->primary_selection_manager != nullptr;
}

std::string WaylandBackend::primary_selection_text() const {
    if (impl_->input != nullptr) {
        return impl_->input->primary_selection_text();
    }
    return impl_->primary_selection_text_cache;
}

void WaylandBackend::set_primary_selection_text(std::string_view text) {
    impl_->primary_selection_text_cache = std::string(text);
    if (impl_->input != nullptr) {
        impl_->input->set_primary_selection_text(impl_->primary_selection_text_cache);
    }
}

SystemPreferences WaylandBackend::system_preferences() const {
    return query_linux_preferences();
}

bool WaylandBackend::supports_system_preferences_observation() const {
    // GSettings-backed observation is GNOME-only; portal Settings observation works on any
    // compositor that exposes xdg-desktop-portal. Either source is enough for live updates.
    if (detect_linux_desktop_environment() == DesktopEnvironment::Gnome &&
        has_gsettings_schema("org.gnome.desktop.interface")) {
        return true;
    }
    GDBusConnection* connection = portal_session_bus_connection();
    if (connection == nullptr) {
        return false;
    }
    const bool portal_available = session_bus_name_has_owner(connection, PortalBusName);
    g_object_unref(connection);
    return portal_available;
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

        // Per-thread portal connection so the SettingChanged signal dispatches to THIS thread's
        // GMainContext. portal_session_bus_connection() uses g_bus_get_sync() which returns the
        // shared session bus; the subscription's callbacks run on whatever context is default for
        // the calling thread at subscribe time (which is why we push the thread-default above).
        GDBusConnection* portal_connection = portal_session_bus_connection();
        guint portal_subscription = 0;
        if (portal_connection != nullptr) {
            if (session_bus_name_has_owner(portal_connection, PortalBusName)) {
                portal_subscription = g_dbus_connection_signal_subscribe(portal_connection,
                                                                         PortalBusName,
                                                                         PortalSettingsInterface,
                                                                         "SettingChanged",
                                                                         PortalObjectPath,
                                                                         nullptr,
                                                                         G_DBUS_SIGNAL_FLAGS_NONE,
                                                                         on_portal_setting_changed,
                                                                         impl,
                                                                         nullptr);
            } else {
                g_object_unref(portal_connection);
                portal_connection = nullptr;
            }
        }

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

        const bool any_source =
            (interface_settings != nullptr || a11y_settings != nullptr || portal_subscription != 0);
        if (!stop_requested && any_source) {
            g_main_loop_run(loop);
        }

        if (portal_subscription != 0 && portal_connection != nullptr) {
            g_dbus_connection_signal_unsubscribe(portal_connection, portal_subscription);
        }
        if (portal_connection != nullptr) {
            g_object_unref(portal_connection);
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

wl_data_device_manager* WaylandBackend::data_device_manager() const {
    return impl_->data_device_manager;
}

zwp_text_input_manager_v3* WaylandBackend::text_input_manager() const {
    return impl_->text_input_manager;
}

zwp_primary_selection_device_manager_v1* WaylandBackend::primary_selection_manager() const {
    return impl_->primary_selection_manager;
}

wp_cursor_shape_manager_v1* WaylandBackend::cursor_shape_manager() const {
    return impl_->cursor_shape_manager;
}

wp_fractional_scale_manager_v1* WaylandBackend::fractional_scale_manager() const {
    return impl_->fractional_scale_manager;
}

wp_viewporter* WaylandBackend::viewporter() const {
    return impl_->viewporter;
}

WaylandInput* WaylandBackend::input() const {
    return impl_->input.get();
}

int WaylandBackend::output_scale(wl_output* output) const {
    const auto it = impl_->output_scales.find(output);
    return (it != impl_->output_scales.end()) ? std::max(1, it->second) : 1;
}

void WaylandBackend::register_surface(wl_surface* wl_surf, WaylandSurface* surface) {
    impl_->surfaces[wl_surf] = surface;
    schedule_accessibility_sync();
}

void WaylandBackend::unregister_surface(wl_surface* wl_surf) {
    impl_->surfaces.erase(wl_surf);
    schedule_accessibility_sync();
}

void WaylandBackend::schedule_accessibility_sync() {
    // Build the snapshot here (main thread) because widgets aren't thread-safe. Then hand the
    // immutable copy to the a11y worker via the cache, and ask it to re-register objects.
    auto snapshot = build_atspi_snapshot_state(*impl_);
    GMainContext* context = nullptr;
    {
        std::lock_guard lock(impl_->accessibility_mutex);
        impl_->cached_accessibility_snapshot = std::move(snapshot);
        context = impl_->accessibility_context;
    }
    if (context != nullptr) {
        g_main_context_invoke(context, sync_accessibility_objects_trampoline, impl_.get());
    }
}

WaylandSurface* WaylandBackend::find_surface(wl_surface* wl_surf) const {
    const auto it = impl_->surfaces.find(wl_surf);
    return (it != impl_->surfaces.end()) ? it->second : nullptr;
}

} // namespace nk
