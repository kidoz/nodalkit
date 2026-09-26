#pragma once

#include "../../accessibility/atspi_tree_snapshot.h"

#include <cstdint>
#include <functional>
#include <gio/gio.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nk::detail {

inline constexpr const char* AtspiApplicationPath = "/org/a11y/atspi/accessible/root";

struct AtspiActionTarget {
    std::uint64_t window = 0;
    AccessibleId node = 0;
};

// Everything the accessibility bus serves, built on the UI thread.
struct AtspiPublishedTree {
    std::vector<AtspiTreeNode> nodes{}; ///< nodes[0] is the application root.
    std::unordered_map<std::string, AtspiActionTarget> targets{};
    std::unordered_map<std::string, std::size_t> index{}; ///< Filled by publish().
};

// Serves the published tree over AT-SPI on the accessibility bus thread:
// registers one D-Bus object per node with the interfaces it implements, embeds
// the application with the desktop registry (again whenever the registry
// restarts), and emits the events that lead from one published tree to the
// next. Readers take a shared immutable tree, so the UI thread never waits on a
// client and a client never sees a half-built tree.
class AtspiDbusServer {
public:
    // Both sinks are called on the bus thread and must hand work to the UI thread.
    using ActionSink = std::function<void(AtspiActionTarget, AccessibleAction)>;
    using RefreshSink = std::function<void()>;

    AtspiDbusServer(std::string toolkit_version, ActionSink actions, RefreshSink refresh);
    ~AtspiDbusServer();

    AtspiDbusServer(const AtspiDbusServer&) = delete;
    AtspiDbusServer& operator=(const AtspiDbusServer&) = delete;
    AtspiDbusServer(AtspiDbusServer&&) = delete;
    AtspiDbusServer& operator=(AtspiDbusServer&&) = delete;

    // UI thread: store a new tree and queue the events that describe the change.
    void publish(AtspiPublishedTree tree);

    // Bus thread.
    void attach(GDBusConnection* connection);
    void sync();
    void detach();

    // Handler entry points; public for the C callback tables.
    void handle_method(GDBusConnection* connection,
                       const char* object_path,
                       const char* interface_name,
                       const char* method_name,
                       GVariant* parameters,
                       GDBusMethodInvocation* invocation);
    GVariant* handle_get_property(GDBusConnection* connection,
                                  const char* object_path,
                                  const char* interface_name,
                                  const char* property_name,
                                  GError** error);
    bool
    handle_set_property(const char* interface_name, const char* property_name, GVariant* value);
    void embedded(GVariant* reply);
    void registry_appeared();
    void registry_vanished();

private:
    [[nodiscard]] std::shared_ptr<const AtspiPublishedTree> tree() const;
    void embed();

    std::string toolkit_version_;
    ActionSink actions_;
    RefreshSink refresh_;

    mutable std::mutex mutex_;
    std::shared_ptr<const AtspiPublishedTree> tree_;
    std::vector<AtspiEvent> pending_events_;
    std::string desktop_name_;
    std::string desktop_path_;
    int application_id_ = 0;

    // Bus thread only.
    GDBusConnection* connection_ = nullptr;
    guint registry_watch_ = 0;
    guint cache_registration_ = 0;
    std::unordered_map<std::string, std::vector<guint>> registrations_;
};

} // namespace nk::detail
