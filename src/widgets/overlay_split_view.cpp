#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/overlay_split_view.h>

namespace nk {

namespace {

class AdaptiveScrim final : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<AdaptiveScrim> create() {
        return std::shared_ptr<AdaptiveScrim>(new AdaptiveScrim());
    }

    Signal<>& on_dismissed() { return dismissed_; }

    bool handle_mouse_event(const MouseEvent& event) override {
        if (event.type == MouseEvent::Type::Release && event.button == 1 &&
            allocation().contains({event.x, event.y})) {
            dismissed_.emit();
            return true;
        }
        return event.type == MouseEvent::Type::Press && event.button == 1;
    }

protected:
    void snapshot(SnapshotContext& ctx) const override {
        ctx.add_color_rect(allocation(), theme_color("background", Color{0.0F, 0.0F, 0.0F, 0.24F}));
    }

private:
    AdaptiveScrim() {
        add_style_class("adaptive-scrim");
        ensure_accessible().set_hidden(true);
    }

    Signal<> dismissed_;
};

class AdaptiveSidebarSurface final : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<AdaptiveSidebarSurface> create() {
        return std::shared_ptr<AdaptiveSidebarSurface>(new AdaptiveSidebarSurface());
    }

    [[nodiscard]] bool hit_test(Point /*point*/) const override { return false; }

protected:
    void snapshot(SnapshotContext& ctx) const override {
        ctx.add_color_rect(
            allocation(),
            theme_color("background", theme_color("sidebar-bg", Color::from_rgb(246, 247, 249))));
        ctx.add_color_rect(
            {allocation().right() - 1.0F, allocation().y, 1.0F, allocation().height},
            theme_color("border-color",
                        theme_color("sidebar-border", Color::from_rgb(224, 228, 234))));
    }

private:
    AdaptiveSidebarSurface() {
        add_style_class("adaptive-sidebar-surface");
        ensure_accessible().set_hidden(true);
    }
};

} // namespace

struct OverlaySplitView::Impl {
    std::shared_ptr<Widget> sidebar;
    std::shared_ptr<Widget> content;
    std::shared_ptr<AdaptiveScrim> scrim;
    std::shared_ptr<AdaptiveSidebarSurface> sidebar_surface;
    ScopedConnection scrim_connection;
    Rect sidebar_bounds{};
    float sidebar_width = 280.0F;
    bool collapsed = false;
    bool show_sidebar = true;
    Signal<bool> collapsed_changed;
    Signal<bool> show_sidebar_changed;
};

std::shared_ptr<OverlaySplitView> OverlaySplitView::create() {
    return std::shared_ptr<OverlaySplitView>(new OverlaySplitView());
}

OverlaySplitView::OverlaySplitView() : impl_(std::make_unique<Impl>()) {
    impl_->scrim = AdaptiveScrim::create();
    impl_->sidebar_surface = AdaptiveSidebarSurface::create();
    append_child(impl_->scrim);
    append_child(impl_->sidebar_surface);
    impl_->scrim_connection =
        ScopedConnection(impl_->scrim->on_dismissed().connect([this] { set_show_sidebar(false); }));
    add_style_class("overlay-split-view");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name("Overlay Split View");
}

OverlaySplitView::~OverlaySplitView() = default;

void OverlaySplitView::set_sidebar(std::shared_ptr<Widget> sidebar) {
    if (impl_->sidebar == sidebar) {
        return;
    }
    if (impl_->sidebar != nullptr) {
        remove_child(*impl_->sidebar);
    }
    impl_->sidebar = std::move(sidebar);
    if (impl_->sidebar != nullptr) {
        append_child(impl_->sidebar);
    }
    queue_layout();
}

Widget* OverlaySplitView::sidebar() const {
    return impl_->sidebar.get();
}

void OverlaySplitView::set_content(std::shared_ptr<Widget> content) {
    if (impl_->content == content) {
        return;
    }
    if (impl_->content != nullptr) {
        remove_child(*impl_->content);
    }
    impl_->content = std::move(content);
    if (impl_->content != nullptr) {
        insert_child(0, impl_->content);
    }
    queue_layout();
}

Widget* OverlaySplitView::content() const {
    return impl_->content.get();
}

bool OverlaySplitView::is_collapsed() const {
    return impl_->collapsed;
}

void OverlaySplitView::set_collapsed(bool collapsed) {
    if (impl_->collapsed == collapsed) {
        return;
    }
    impl_->collapsed = collapsed;
    impl_->collapsed_changed.emit(collapsed);
    queue_layout();
}

bool OverlaySplitView::shows_sidebar() const {
    return impl_->show_sidebar;
}

void OverlaySplitView::set_show_sidebar(bool show_sidebar) {
    if (impl_->show_sidebar == show_sidebar) {
        return;
    }
    impl_->show_sidebar = show_sidebar;
    impl_->show_sidebar_changed.emit(show_sidebar);
    queue_layout();
}

float OverlaySplitView::sidebar_width() const {
    return impl_->sidebar_width;
}

void OverlaySplitView::set_sidebar_width(float width) {
    width = std::max(0.0F, width);
    if (impl_->sidebar_width != width) {
        impl_->sidebar_width = width;
        queue_layout();
    }
}

Signal<bool>& OverlaySplitView::on_collapsed_changed() {
    return impl_->collapsed_changed;
}

Signal<bool>& OverlaySplitView::on_show_sidebar_changed() {
    return impl_->show_sidebar_changed;
}

SizeRequest OverlaySplitView::measure(const Constraints& constraints) const {
    const auto sidebar_request = impl_->sidebar != nullptr
                                     ? impl_->sidebar->measure_for_diagnostics(constraints)
                                     : SizeRequest{};
    const auto content_request = impl_->content != nullptr
                                     ? impl_->content->measure_for_diagnostics(constraints)
                                     : SizeRequest{};
    if (impl_->collapsed) {
        return content_request;
    }
    const float separator = theme_number("separator-width", 1.0F);
    return {
        sidebar_request.minimum_width + content_request.minimum_width + separator,
        std::max(sidebar_request.minimum_height, content_request.minimum_height),
        sidebar_request.natural_width + content_request.natural_width + separator,
        std::max(sidebar_request.natural_height, content_request.natural_height),
    };
}

void OverlaySplitView::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (impl_->content != nullptr) {
        impl_->content->set_visible(true);
    }
    const float sidebar_width = std::min(impl_->sidebar_width, allocation.width);
    impl_->sidebar_bounds = {allocation.x, allocation.y, sidebar_width, allocation.height};

    if (impl_->collapsed) {
        if (impl_->content != nullptr) {
            impl_->content->allocate(allocation);
        }
        impl_->scrim->set_visible(impl_->show_sidebar);
        impl_->sidebar_surface->set_visible(impl_->show_sidebar && impl_->sidebar != nullptr);
        if (impl_->show_sidebar) {
            impl_->scrim->allocate(allocation);
            impl_->sidebar_surface->allocate(impl_->sidebar_bounds);
        }
        if (impl_->sidebar != nullptr) {
            impl_->sidebar->set_visible(impl_->show_sidebar);
            if (impl_->show_sidebar) {
                impl_->sidebar->allocate(impl_->sidebar_bounds);
            }
        }
        return;
    }

    impl_->scrim->set_visible(false);
    impl_->sidebar_surface->set_visible(impl_->sidebar != nullptr);
    if (impl_->sidebar != nullptr) {
        impl_->sidebar_surface->allocate(impl_->sidebar_bounds);
    }
    if (impl_->sidebar != nullptr) {
        impl_->sidebar->set_visible(true);
        impl_->sidebar->allocate(impl_->sidebar_bounds);
    }
    if (impl_->content != nullptr) {
        const float separator = theme_number("separator-width", 1.0F);
        impl_->content->allocate({allocation.x + sidebar_width + separator,
                                  allocation.y,
                                  std::max(0.0F, allocation.width - sidebar_width - separator),
                                  allocation.height});
    }
}

void OverlaySplitView::snapshot(SnapshotContext& ctx) const {
    Widget::snapshot(ctx);
}

} // namespace nk
