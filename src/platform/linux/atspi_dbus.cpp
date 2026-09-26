#include "atspi_dbus.h"

#include "../../text/text_boundaries.h"

#include <algorithm>
#include <nk/foundation/logging.h>
#include <string_view>
#include <utility>

namespace nk::detail {

namespace {

constexpr const char* AccessibleInterface = "org.a11y.atspi.Accessible";
constexpr const char* ApplicationInterface = "org.a11y.atspi.Application";
constexpr const char* ComponentInterface = "org.a11y.atspi.Component";
constexpr const char* ActionInterface = "org.a11y.atspi.Action";
constexpr const char* TextInterface = "org.a11y.atspi.Text";
constexpr const char* RegistryName = "org.a11y.atspi.Registry";
constexpr const char* SocketInterface = "org.a11y.atspi.Socket";
constexpr const char* NullPath = "/org/a11y/atspi/null";
constexpr const char* CachePath = "/org/a11y/atspi/cache";
constexpr const char* CacheInterface = "org.a11y.atspi.Cache";

// Members follow at-spi2-core's xml/*.xml; only implemented members are declared,
// so GDBus rejects anything else before it reaches a handler.
constexpr const char* IntrospectionXml = R"xml(
<node>
  <interface name="org.a11y.atspi.Accessible">
    <property name="version" type="u" access="read"/>
    <property name="Name" type="s" access="read"/>
    <property name="Description" type="s" access="read"/>
    <property name="Parent" type="(so)" access="read"/>
    <property name="ChildCount" type="i" access="read"/>
    <property name="Locale" type="s" access="read"/>
    <property name="AccessibleId" type="s" access="read"/>
    <property name="HelpText" type="s" access="read"/>
    <method name="GetChildAtIndex">
      <arg direction="in" name="index" type="i"/>
      <arg direction="out" type="(so)"/>
    </method>
    <method name="GetChildren"><arg direction="out" type="a(so)"/></method>
    <method name="GetIndexInParent"><arg direction="out" type="i"/></method>
    <method name="GetRelationSet"><arg direction="out" type="a(ua(so))"/></method>
    <method name="GetRole"><arg direction="out" type="u"/></method>
    <method name="GetRoleName"><arg direction="out" type="s"/></method>
    <method name="GetLocalizedRoleName"><arg direction="out" type="s"/></method>
    <method name="GetState"><arg direction="out" type="au"/></method>
    <method name="GetAttributes"><arg direction="out" type="a{ss}"/></method>
    <method name="GetApplication"><arg direction="out" type="(so)"/></method>
    <method name="GetInterfaces"><arg direction="out" type="as"/></method>
  </interface>
  <interface name="org.a11y.atspi.Application">
    <property name="ToolkitName" type="s" access="read"/>
    <property name="Version" type="s" access="read"/>
    <property name="ToolkitVersion" type="s" access="read"/>
    <property name="AtspiVersion" type="s" access="read"/>
    <property name="InterfaceVersion" type="u" access="read"/>
    <property name="Id" type="i" access="readwrite"/>
    <method name="GetLocale">
      <arg direction="in" name="lctype" type="u"/>
      <arg direction="out" type="s"/>
    </method>
    <method name="GetApplicationBusAddress"><arg direction="out" type="s"/></method>
  </interface>
  <interface name="org.a11y.atspi.Component">
    <property name="version" type="u" access="read"/>
    <method name="Contains">
      <arg direction="in" name="x" type="i"/>
      <arg direction="in" name="y" type="i"/>
      <arg direction="in" name="coord_type" type="u"/>
      <arg direction="out" type="b"/>
    </method>
    <method name="GetAccessibleAtPoint">
      <arg direction="in" name="x" type="i"/>
      <arg direction="in" name="y" type="i"/>
      <arg direction="in" name="coord_type" type="u"/>
      <arg direction="out" type="(so)"/>
    </method>
    <method name="GetExtents">
      <arg direction="in" name="coord_type" type="u"/>
      <arg direction="out" type="(iiii)"/>
    </method>
    <method name="GetPosition">
      <arg direction="in" name="coord_type" type="u"/>
      <arg direction="out" name="x" type="i"/>
      <arg direction="out" name="y" type="i"/>
    </method>
    <method name="GetSize">
      <arg direction="out" name="width" type="i"/>
      <arg direction="out" name="height" type="i"/>
    </method>
    <method name="GetLayer"><arg direction="out" type="u"/></method>
    <method name="GetMDIZOrder"><arg direction="out" type="n"/></method>
    <method name="GrabFocus"><arg direction="out" type="b"/></method>
    <method name="GetAlpha"><arg direction="out" type="d"/></method>
  </interface>
  <interface name="org.a11y.atspi.Action">
    <property name="version" type="u" access="read"/>
    <property name="NActions" type="i" access="read"/>
    <method name="GetDescription">
      <arg direction="in" name="index" type="i"/>
      <arg direction="out" type="s"/>
    </method>
    <method name="GetName">
      <arg direction="in" name="index" type="i"/>
      <arg direction="out" type="s"/>
    </method>
    <method name="GetLocalizedName">
      <arg direction="in" name="index" type="i"/>
      <arg direction="out" type="s"/>
    </method>
    <method name="GetKeyBinding">
      <arg direction="in" name="index" type="i"/>
      <arg direction="out" type="s"/>
    </method>
    <method name="GetActions"><arg direction="out" type="a(sss)"/></method>
    <method name="DoAction">
      <arg direction="in" name="index" type="i"/>
      <arg direction="out" type="b"/>
    </method>
  </interface>
  <interface name="org.a11y.atspi.Cache">
    <property name="version" type="u" access="read"/>
    <method name="GetItems"><arg direction="out" type="a((so)(so)(so)iiassusau)"/></method>
    <signal name="AddAccessible"><arg type="((so)(so)(so)iiassusau)"/></signal>
    <signal name="RemoveAccessible"><arg type="(so)"/></signal>
  </interface>
  <interface name="org.a11y.atspi.Text">
    <property name="version" type="u" access="read"/>
    <property name="CharacterCount" type="i" access="read"/>
    <property name="CaretOffset" type="i" access="read"/>
    <method name="GetText">
      <arg direction="in" name="start_offset" type="i"/>
      <arg direction="in" name="end_offset" type="i"/>
      <arg direction="out" type="s"/>
    </method>
    <method name="GetCharacterAtOffset">
      <arg direction="in" name="offset" type="i"/>
      <arg direction="out" type="i"/>
    </method>
    <method name="GetStringAtOffset">
      <arg direction="in" name="offset" type="i"/>
      <arg direction="in" name="granularity" type="u"/>
      <arg direction="out" type="s"/>
      <arg direction="out" name="start_offset" type="i"/>
      <arg direction="out" name="end_offset" type="i"/>
    </method>
    <method name="GetNSelections"><arg direction="out" type="i"/></method>
    <method name="GetSelection">
      <arg direction="in" name="selection_num" type="i"/>
      <arg direction="out" name="start_offset" type="i"/>
      <arg direction="out" name="end_offset" type="i"/>
    </method>
  </interface>
</node>
)xml";

GDBusNodeInfo* introspection() {
    static GDBusNodeInfo* info = [] {
        GError* error = nullptr;
        GDBusNodeInfo* parsed = g_dbus_node_info_new_for_xml(IntrospectionXml, &error);
        if (parsed == nullptr && error != nullptr) {
            NK_LOG_ERROR("WaylandA11y", error->message);
            g_error_free(error);
        }
        return parsed;
    }();
    return info;
}

GVariant* utf8_string(const std::string& value) {
    const bool valid = g_utf8_validate(value.c_str(), static_cast<gssize>(value.size()), nullptr);
    return g_variant_new_string(valid ? value.c_str() : "");
}

GVariant* reference(const char* bus_name, const std::string& path) {
    return g_variant_new("(so)", bus_name, path.empty() ? NullPath : path.c_str());
}

const AtspiTreeNode* find_node(const AtspiPublishedTree& tree, std::string_view path) {
    const auto found = tree.index.find(std::string(path));
    return found != tree.index.end() ? &tree.nodes[found->second] : nullptr;
}

int index_in_parent(const AtspiPublishedTree& tree, const AtspiTreeNode& node) {
    const auto* parent = find_node(tree, node.node.parent_path);
    if (parent == nullptr) {
        return -1;
    }
    const auto& siblings = parent->node.child_paths;
    const auto found = std::ranges::find(siblings, node.node.object_path);
    return found != siblings.end() ? static_cast<int>(found - siblings.begin()) : -1;
}

// Bounds are window-relative: Wayland does not reveal where a window sits on
// screen, so screen and window coordinates coincide. Parent coordinates
// subtract the parent's origin.
Rect extents(const AtspiPublishedTree& tree, const AtspiTreeNode& node, guint32 coord_type) {
    auto bounds = node.node.bounds;
    if (coord_type == 2U) {
        if (const auto* parent = find_node(tree, node.node.parent_path); parent != nullptr) {
            bounds.x -= parent->node.bounds.x;
            bounds.y -= parent->node.bounds.y;
        }
    }
    return bounds;
}

const AtspiTreeNode*
node_at_point(const AtspiPublishedTree& tree, const AtspiTreeNode& node, Point point) {
    const auto& children = node.node.child_paths;
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const auto* child = find_node(tree, *it);
        if (child == nullptr) {
            continue;
        }
        if (const auto* hit = node_at_point(tree, *child, point); hit != nullptr) {
            return hit;
        }
        if (child->node.bounds.contains(point)) {
            return child;
        }
    }
    return nullptr;
}

// Character-granularity helpers over UTF-8 values; offsets count characters.
std::pair<int, int> string_range(std::string_view text, int offset, guint32 granularity) {
    const auto units = decode_utf8_units(text);
    const int count = static_cast<int>(units.size());
    if (offset < 0 || offset >= count) {
        return {count, count};
    }
    const auto is_space = [&](int index) {
        const auto code = units[static_cast<std::size_t>(index)].code_point;
        return code == U' ' || code == U'\t' || code == U'\n';
    };
    const auto is_newline = [&](int index) {
        return units[static_cast<std::size_t>(index)].code_point == U'\n';
    };
    switch (granularity) {
    case 0U: // Character.
        return {offset, offset + 1};
    case 1U: { // Word: the run of non-space characters containing the offset.
        int start = offset;
        int end = offset;
        while (start > 0 && !is_space(start - 1)) {
            --start;
        }
        while (end < count && !is_space(end)) {
            ++end;
        }
        return {start, end};
    }
    case 3U:   // Line.
    case 4U: { // Paragraph: lines are hard lines.
        int start = offset;
        int end = offset;
        while (start > 0 && !is_newline(start - 1)) {
            --start;
        }
        while (end < count && !is_newline(end)) {
            ++end;
        }
        return {start, std::min(end + 1, count)};
    }
    default: // Sentence: the whole text.
        return {0, count};
    }
}

// One Cache.GetItems element: self, application, and parent references, index in
// parent, child count, interfaces, name, role, description, and states.
GVariant*
cache_item(const AtspiPublishedTree& tree, const AtspiTreeNode& node, const char* bus_name) {
    GVariantBuilder interfaces;
    g_variant_builder_init(&interfaces, G_VARIANT_TYPE("as"));
    for (const auto& name : node.node.interfaces) {
        g_variant_builder_add(&interfaces, "s", name.c_str());
    }
    GVariantBuilder states;
    g_variant_builder_init(&states, G_VARIANT_TYPE("au"));
    for (const auto word : node.states) {
        g_variant_builder_add(&states, "u", word);
    }
    const bool application = node.role == AtspiRoleValue::Application;
    return g_variant_new("(@(so)@(so)@(so)ii@as@su@s@au)",
                         reference(bus_name, node.node.object_path),
                         reference(bus_name, AtspiApplicationPath),
                         application ? reference("", "")
                                     : reference(bus_name, node.node.parent_path),
                         application ? -1 : index_in_parent(tree, node),
                         static_cast<gint32>(node.node.child_paths.size()),
                         g_variant_builder_end(&interfaces),
                         utf8_string(node.node.name),
                         static_cast<guint32>(node.role),
                         utf8_string(node.node.description),
                         g_variant_builder_end(&states));
}

GVariant* signal_data(const AtspiEvent& event, const char* bus_name) {
    switch (event.data) {
    case AtspiEvent::Data::Text:
        return utf8_string(event.data_value);
    case AtspiEvent::Data::Object:
        return reference(bus_name, event.data_value);
    case AtspiEvent::Data::None:
        break;
    }
    return g_variant_new_int32(0);
}

void on_method(GDBusConnection* connection,
               const gchar* /*sender*/,
               const gchar* object_path,
               const gchar* interface_name,
               const gchar* method_name,
               GVariant* parameters,
               GDBusMethodInvocation* invocation,
               gpointer user_data) {
    static_cast<AtspiDbusServer*>(user_data)->handle_method(
        connection, object_path, interface_name, method_name, parameters, invocation);
}

GVariant* on_get_property(GDBusConnection* connection,
                          const gchar* /*sender*/,
                          const gchar* object_path,
                          const gchar* interface_name,
                          const gchar* property_name,
                          GError** error,
                          gpointer user_data) {
    return static_cast<AtspiDbusServer*>(user_data)->handle_get_property(
        connection, object_path, interface_name, property_name, error);
}

gboolean on_set_property(GDBusConnection* /*connection*/,
                         const gchar* /*sender*/,
                         const gchar* /*object_path*/,
                         const gchar* interface_name,
                         const gchar* property_name,
                         GVariant* value,
                         GError** error,
                         gpointer user_data) {
    if (static_cast<AtspiDbusServer*>(user_data)->handle_set_property(
            interface_name, property_name, value)) {
        return TRUE;
    }
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_PROPERTY_READ_ONLY, "Read-only property");
    return FALSE;
}

constexpr GDBusInterfaceVTable vtable = {
    .method_call = on_method,
    .get_property = on_get_property,
    .set_property = on_set_property,
    .padding = {},
};

void on_embedded(GObject* source, GAsyncResult* result, gpointer user_data) {
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (reply == nullptr) {
        if (error != nullptr) {
            NK_LOG_WARN("WaylandA11y", error->message);
            g_error_free(error);
        }
        return;
    }
    static_cast<AtspiDbusServer*>(user_data)->embedded(reply);
    g_variant_unref(reply);
}

void on_registry_appeared(GDBusConnection* /*connection*/,
                          const gchar* /*name*/,
                          const gchar* /*owner*/,
                          gpointer user_data) {
    static_cast<AtspiDbusServer*>(user_data)->registry_appeared();
}

void on_registry_vanished(GDBusConnection* /*connection*/,
                          const gchar* /*name*/,
                          gpointer user_data) {
    static_cast<AtspiDbusServer*>(user_data)->registry_vanished();
}

} // namespace

AtspiDbusServer::AtspiDbusServer(std::string toolkit_version,
                                 ActionSink actions,
                                 RefreshSink refresh)
    : toolkit_version_(std::move(toolkit_version))
    , actions_(std::move(actions))
    , refresh_(std::move(refresh))
    , tree_(std::make_shared<AtspiPublishedTree>()) {}

AtspiDbusServer::~AtspiDbusServer() = default;

void AtspiDbusServer::publish(AtspiPublishedTree tree) {
    tree.index.clear();
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        tree.index.emplace(tree.nodes[i].node.object_path, i);
    }
    auto next = std::make_shared<const AtspiPublishedTree>(std::move(tree));
    std::lock_guard lock(mutex_);
    if (!tree_->nodes.empty()) {
        auto events = diff_atspi_nodes(tree_->nodes, next->nodes);
        pending_events_.insert(pending_events_.end(),
                               std::make_move_iterator(events.begin()),
                               std::make_move_iterator(events.end()));
    }
    tree_ = std::move(next);
}

std::shared_ptr<const AtspiPublishedTree> AtspiDbusServer::tree() const {
    std::lock_guard lock(mutex_);
    return tree_;
}

void AtspiDbusServer::attach(GDBusConnection* connection) {
    connection_ = connection;
    if (auto* info = introspection(); info != nullptr) {
        GError* error = nullptr;
        cache_registration_ = g_dbus_connection_register_object(
            connection_,
            CachePath,
            g_dbus_node_info_lookup_interface(info, CacheInterface),
            &vtable,
            this,
            nullptr,
            &error);
        if (cache_registration_ == 0U && error != nullptr) {
            NK_LOG_WARN("WaylandA11y", error->message);
            g_error_free(error);
        }
    }
    sync();
    registry_watch_ = g_bus_watch_name_on_connection(connection_,
                                                     RegistryName,
                                                     G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                     on_registry_appeared,
                                                     on_registry_vanished,
                                                     this,
                                                     nullptr);
}

void AtspiDbusServer::detach() {
    if (connection_ == nullptr) {
        return;
    }
    if (registry_watch_ != 0U) {
        g_bus_unwatch_name(registry_watch_);
        registry_watch_ = 0;
    }
    for (auto& [path, ids] : registrations_) {
        for (const guint id : ids) {
            g_dbus_connection_unregister_object(connection_, id);
        }
    }
    registrations_.clear();
    if (cache_registration_ != 0U) {
        g_dbus_connection_unregister_object(connection_, cache_registration_);
        cache_registration_ = 0;
    }
    connection_ = nullptr;
}

void AtspiDbusServer::sync() {
    if (connection_ == nullptr) {
        return;
    }
    const auto current = tree();
    const char* bus_name = g_dbus_connection_get_unique_name(connection_);
    const bool announce = !registrations_.empty();
    for (auto it = registrations_.begin(); it != registrations_.end();) {
        if (!current->index.contains(it->first)) {
            for (const guint id : it->second) {
                g_dbus_connection_unregister_object(connection_, id);
            }
            g_dbus_connection_emit_signal(connection_,
                                          nullptr,
                                          CachePath,
                                          CacheInterface,
                                          "RemoveAccessible",
                                          g_variant_new("(@(so))", reference(bus_name, it->first)),
                                          nullptr);
            it = registrations_.erase(it);
        } else {
            ++it;
        }
    }
    auto* info = introspection();
    for (const auto& node : current->nodes) {
        if (info == nullptr || registrations_.contains(node.node.object_path)) {
            continue;
        }
        auto& ids = registrations_[node.node.object_path];
        for (const auto& interface_name : node.node.interfaces) {
            auto* interface_info = g_dbus_node_info_lookup_interface(info, interface_name.c_str());
            if (interface_info == nullptr) {
                continue;
            }
            GError* error = nullptr;
            const guint id = g_dbus_connection_register_object(connection_,
                                                               node.node.object_path.c_str(),
                                                               interface_info,
                                                               &vtable,
                                                               this,
                                                               nullptr,
                                                               &error);
            if (id == 0U) {
                if (error != nullptr) {
                    NK_LOG_WARN("WaylandA11y", error->message);
                    g_error_free(error);
                }
                continue;
            }
            ids.push_back(id);
        }
        if (announce) {
            g_dbus_connection_emit_signal(
                connection_,
                nullptr,
                CachePath,
                CacheInterface,
                "AddAccessible",
                g_variant_new("(@((so)(so)(so)iiassusau))", cache_item(*current, node, bus_name)),
                nullptr);
        }
    }

    // Emit only after new objects are registered, so a client that reacts to
    // children-changed:add can already query the child.
    std::vector<AtspiEvent> events;
    {
        std::lock_guard lock(mutex_);
        events.swap(pending_events_);
    }
    for (const auto& event : events) {
        GVariantBuilder properties;
        g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
        g_dbus_connection_emit_signal(connection_,
                                      nullptr,
                                      event.object_path.c_str(),
                                      event.interface_name.c_str(),
                                      event.member.c_str(),
                                      g_variant_new("(siiva{sv})",
                                                    event.detail.c_str(),
                                                    event.detail1,
                                                    event.detail2,
                                                    signal_data(event, bus_name),
                                                    &properties),
                                      nullptr);
    }
}

void AtspiDbusServer::embed() {
    if (connection_ == nullptr) {
        return;
    }
    g_dbus_connection_call(connection_,
                           RegistryName,
                           AtspiApplicationPath,
                           SocketInterface,
                           "Embed",
                           g_variant_new("((so))",
                                         g_dbus_connection_get_unique_name(connection_),
                                         AtspiApplicationPath),
                           G_VARIANT_TYPE("((so))"),
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           nullptr,
                           on_embedded,
                           this);
}

void AtspiDbusServer::embedded(GVariant* reply) {
    const gchar* name = nullptr;
    const gchar* path = nullptr;
    g_variant_get(reply, "((&s&o))", &name, &path);
    std::lock_guard lock(mutex_);
    desktop_name_ = name;
    desktop_path_ = path;
}

void AtspiDbusServer::registry_appeared() {
    embed();
}

void AtspiDbusServer::registry_vanished() {
    std::lock_guard lock(mutex_);
    desktop_name_.clear();
    desktop_path_.clear();
}

void AtspiDbusServer::handle_method(GDBusConnection* connection,
                                    const char* object_path,
                                    const char* interface_name,
                                    const char* method_name,
                                    GVariant* parameters,
                                    GDBusMethodInvocation* invocation) {
    if (refresh_) {
        refresh_();
    }
    const auto current = tree();
    if (std::string_view(interface_name) == CacheInterface) {
        GVariantBuilder items;
        g_variant_builder_init(&items, G_VARIANT_TYPE("a((so)(so)(so)iiassusau)"));
        for (const auto& item : current->nodes) {
            g_variant_builder_add_value(
                &items, cache_item(*current, item, g_dbus_connection_get_unique_name(connection)));
        }
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(a((so)(so)(so)iiassusau))", &items));
        return;
    }
    const auto* node = find_node(*current, object_path);
    if (node == nullptr) {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.a11y.atspi.Error.NotFound", "Accessible object not found");
        return;
    }
    const char* bus_name = g_dbus_connection_get_unique_name(connection);
    const std::string_view interface(interface_name);
    const std::string_view method(method_name);
    const auto reply = [invocation](GVariant* value) {
        g_dbus_method_invocation_return_value(invocation, value);
    };
    const auto child_count = static_cast<int>(node->node.child_paths.size());

    if (interface == AccessibleInterface) {
        if (method == "GetChildAtIndex") {
            gint32 index = -1;
            g_variant_get(parameters, "(i)", &index);
            const bool valid = index >= 0 && index < child_count;
            const std::string empty;
            const auto& path =
                valid ? node->node.child_paths[static_cast<std::size_t>(index)] : empty;
            reply(g_variant_new("(@(so))", reference(bus_name, path)));
            return;
        }
        if (method == "GetChildren") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("a(so)"));
            for (const auto& child : node->node.child_paths) {
                g_variant_builder_add_value(&builder, reference(bus_name, child));
            }
            reply(g_variant_new("(a(so))", &builder));
            return;
        }
        if (method == "GetIndexInParent") {
            reply(g_variant_new("(i)", index_in_parent(*current, *node)));
            return;
        }
        if (method == "GetRelationSet") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ua(so))"));
            reply(g_variant_new("(a(ua(so)))", &builder));
            return;
        }
        if (method == "GetRole") {
            reply(g_variant_new("(u)", static_cast<guint32>(node->role)));
            return;
        }
        if (method == "GetRoleName" || method == "GetLocalizedRoleName") {
            reply(g_variant_new("(s)", node->node.role_name.c_str()));
            return;
        }
        if (method == "GetState") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
            for (const auto word : node->states) {
                g_variant_builder_add(&builder, "u", word);
            }
            reply(g_variant_new("(au)", &builder));
            return;
        }
        if (method == "GetAttributes") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
            g_variant_builder_add(&builder, "{ss}", "toolkit", "NodalKit");
            reply(g_variant_new("(a{ss})", &builder));
            return;
        }
        if (method == "GetApplication") {
            reply(g_variant_new("(@(so))", reference(bus_name, AtspiApplicationPath)));
            return;
        }
        if (method == "GetInterfaces") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
            for (const auto& name : node->node.interfaces) {
                g_variant_builder_add(&builder, "s", name.c_str());
            }
            reply(g_variant_new("(as)", &builder));
            return;
        }
    } else if (interface == ApplicationInterface) {
        if (method == "GetLocale" || method == "GetApplicationBusAddress") {
            reply(g_variant_new("(s)", ""));
            return;
        }
    } else if (interface == ComponentInterface) {
        if (method == "GetExtents") {
            guint32 coord_type = 0;
            g_variant_get(parameters, "(u)", &coord_type);
            const auto bounds = extents(*current, *node, coord_type);
            reply(g_variant_new("((iiii))",
                                static_cast<gint32>(bounds.x),
                                static_cast<gint32>(bounds.y),
                                static_cast<gint32>(bounds.width),
                                static_cast<gint32>(bounds.height)));
            return;
        }
        if (method == "GetPosition") {
            guint32 coord_type = 0;
            g_variant_get(parameters, "(u)", &coord_type);
            const auto bounds = extents(*current, *node, coord_type);
            reply(g_variant_new(
                "(ii)", static_cast<gint32>(bounds.x), static_cast<gint32>(bounds.y)));
            return;
        }
        if (method == "GetSize") {
            reply(g_variant_new("(ii)",
                                static_cast<gint32>(node->node.bounds.width),
                                static_cast<gint32>(node->node.bounds.height)));
            return;
        }
        if (method == "Contains" || method == "GetAccessibleAtPoint") {
            gint32 x = 0;
            gint32 y = 0;
            guint32 coord_type = 0;
            g_variant_get(parameters, "(iiu)", &x, &y, &coord_type);
            Point point{.x = static_cast<float>(x), .y = static_cast<float>(y)};
            if (coord_type == 2U) {
                if (const auto* parent = find_node(*current, node->node.parent_path);
                    parent != nullptr) {
                    point.x += parent->node.bounds.x;
                    point.y += parent->node.bounds.y;
                }
            }
            if (method == "Contains") {
                reply(g_variant_new("(b)", node->node.bounds.contains(point) ? TRUE : FALSE));
                return;
            }
            const auto* hit = node_at_point(*current, *node, point);
            reply(g_variant_new("(@(so))",
                                reference(bus_name, hit != nullptr ? hit->node.object_path : "")));
            return;
        }
        if (method == "GetLayer") {
            // ATSPI_LAYER_WINDOW for windows, ATSPI_LAYER_WIDGET for everything else.
            reply(g_variant_new("(u)", node->role == AtspiRoleValue::Frame ? 7U : 3U));
            return;
        }
        if (method == "GetMDIZOrder") {
            reply(g_variant_new("(n)", static_cast<gint16>(0)));
            return;
        }
        if (method == "GetAlpha") {
            reply(g_variant_new("(d)", 1.0));
            return;
        }
        if (method == "GrabFocus") {
            const auto target = current->targets.find(object_path);
            const bool focusable = target != current->targets.end() &&
                                   has_atspi_state(node->states, AtspiStateValue::Focusable);
            if (focusable) {
                actions_(target->second, AccessibleAction::Focus);
            }
            reply(g_variant_new("(b)", focusable ? TRUE : FALSE));
            return;
        }
    } else if (interface == ActionInterface) {
        const auto& names = node->node.action_names;
        const auto action_name = [&](gint32 index) -> std::string {
            return index >= 0 && static_cast<std::size_t>(index) < names.size()
                       ? names[static_cast<std::size_t>(index)]
                       : std::string{};
        };
        if (method == "GetName" || method == "GetLocalizedName") {
            gint32 index = -1;
            g_variant_get(parameters, "(i)", &index);
            reply(g_variant_new("(s)", action_name(index).c_str()));
            return;
        }
        if (method == "GetDescription" || method == "GetKeyBinding") {
            reply(g_variant_new("(s)", ""));
            return;
        }
        if (method == "GetActions") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("a(sss)"));
            for (const auto& name : names) {
                g_variant_builder_add(&builder, "(sss)", name.c_str(), "", "");
            }
            reply(g_variant_new("(a(sss))", &builder));
            return;
        }
        if (method == "DoAction") {
            gint32 index = -1;
            g_variant_get(parameters, "(i)", &index);
            const auto target = current->targets.find(object_path);
            const auto action = atspi_action_from_name(action_name(index));
            // Performed on the UI thread, which also rejects disabled or covered widgets.
            const bool queued = target != current->targets.end() && action.has_value();
            if (queued) {
                actions_(target->second, *action);
            }
            reply(g_variant_new("(b)", queued ? TRUE : FALSE));
            return;
        }
    } else if (interface == TextInterface) {
        const auto& text = node->node.value;
        if (method == "GetText") {
            gint32 start = 0;
            gint32 end = -1;
            g_variant_get(parameters, "(ii)", &start, &end);
            reply(g_variant_new("(@s)", utf8_string(atspi_text_slice(text, start, end))));
            return;
        }
        if (method == "GetCharacterAtOffset") {
            gint32 offset = 0;
            g_variant_get(parameters, "(i)", &offset);
            const auto units = decode_utf8_units(text);
            const auto code =
                offset >= 0 && static_cast<std::size_t>(offset) < units.size()
                    ? static_cast<gint32>(units[static_cast<std::size_t>(offset)].code_point)
                    : 0;
            reply(g_variant_new("(i)", code));
            return;
        }
        if (method == "GetStringAtOffset") {
            gint32 offset = 0;
            guint32 granularity = 0;
            g_variant_get(parameters, "(iu)", &offset, &granularity);
            const auto [start, end] = string_range(text, offset, granularity);
            reply(g_variant_new(
                "(@sii)", utf8_string(atspi_text_slice(text, start, end)), start, end));
            return;
        }
        if (method == "GetNSelections") {
            reply(g_variant_new("(i)", 0));
            return;
        }
        if (method == "GetSelection") {
            reply(g_variant_new("(ii)", 0, 0));
            return;
        }
    }

    g_dbus_method_invocation_return_dbus_error(
        invocation, "org.freedesktop.DBus.Error.UnknownMethod", "Unsupported accessibility method");
}

GVariant* AtspiDbusServer::handle_get_property(GDBusConnection* connection,
                                               const char* object_path,
                                               const char* interface_name,
                                               const char* property_name,
                                               GError** error) {
    if (refresh_) {
        refresh_();
    }
    if (std::string_view(property_name) == "version") {
        return g_variant_new_uint32(1);
    }
    const auto current = tree();
    const auto* node = find_node(*current, object_path);
    if (node == nullptr) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT, "Unknown accessible object");
        return nullptr;
    }
    const std::string_view interface(interface_name);
    const std::string_view property(property_name);
    if (property == "version") {
        return g_variant_new_uint32(1);
    }
    if (interface == AccessibleInterface) {
        if (property == "Name") {
            return utf8_string(node->node.name);
        }
        if (property == "Description") {
            return utf8_string(node->node.description);
        }
        if (property == "Parent") {
            if (node->role == AtspiRoleValue::Application) {
                std::lock_guard lock(mutex_);
                return desktop_name_.empty() ? reference("", "")
                                             : reference(desktop_name_.c_str(), desktop_path_);
            }
            return reference(g_dbus_connection_get_unique_name(connection), node->node.parent_path);
        }
        if (property == "ChildCount") {
            return g_variant_new_int32(static_cast<gint32>(node->node.child_paths.size()));
        }
        if (property == "Locale" || property == "HelpText") {
            return g_variant_new_string("");
        }
        if (property == "AccessibleId") {
            return utf8_string(node->accessible_id);
        }
    } else if (interface == ApplicationInterface) {
        if (property == "ToolkitName") {
            return g_variant_new_string("NodalKit");
        }
        if (property == "Version" || property == "ToolkitVersion") {
            return g_variant_new_string(toolkit_version_.c_str());
        }
        if (property == "AtspiVersion") {
            return g_variant_new_string("2.1");
        }
        if (property == "InterfaceVersion") {
            return g_variant_new_uint32(0);
        }
        if (property == "Id") {
            std::lock_guard lock(mutex_);
            return g_variant_new_int32(application_id_);
        }
    } else if (interface == ActionInterface) {
        if (property == "NActions") {
            return g_variant_new_int32(static_cast<gint32>(node->node.action_names.size()));
        }
    } else if (interface == TextInterface) {
        if (property == "CharacterCount" || property == "CaretOffset") {
            // The caret is reported at the end until editors expose it.
            return g_variant_new_int32(atspi_character_count(node->node.value));
        }
    }
    g_set_error(
        error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown accessibility property");
    return nullptr;
}

bool AtspiDbusServer::handle_set_property(const char* interface_name,
                                          const char* property_name,
                                          GVariant* value) {
    if (std::string_view(interface_name) == ApplicationInterface &&
        std::string_view(property_name) == "Id") {
        std::lock_guard lock(mutex_);
        application_id_ = g_variant_get_int32(value);
        return true;
    }
    return false;
}

} // namespace nk::detail
