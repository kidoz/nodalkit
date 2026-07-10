#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/navigation_split_view.h>

namespace nk {

struct NavigationSplitView::Impl {
    std::shared_ptr<Widget> sidebar;
    std::shared_ptr<Widget> content;
    Rect sidebar_bounds{};
    float sidebar_width_fraction = 0.35F;
    float min_sidebar_width = 180.0F;
    float max_sidebar_width = 360.0F;
    bool collapsed = false;
    bool show_content = false;
    Signal<bool> collapsed_changed;
    Signal<bool> show_content_changed;
};

std::shared_ptr<NavigationSplitView> NavigationSplitView::create() {
    return std::shared_ptr<NavigationSplitView>(new NavigationSplitView());
}

NavigationSplitView::NavigationSplitView() : impl_(std::make_unique<Impl>()) {
    add_style_class("navigation-split-view");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name("Navigation Split View");
}

NavigationSplitView::~NavigationSplitView() = default;

void NavigationSplitView::set_sidebar(std::shared_ptr<Widget> sidebar) {
    if (impl_->sidebar == sidebar) {
        return;
    }
    if (impl_->sidebar != nullptr) {
        remove_child(*impl_->sidebar);
    }
    impl_->sidebar = std::move(sidebar);
    if (impl_->sidebar != nullptr) {
        insert_child(0, impl_->sidebar);
    }
    queue_layout();
}

Widget* NavigationSplitView::sidebar() const {
    return impl_->sidebar.get();
}

void NavigationSplitView::set_content(std::shared_ptr<Widget> content) {
    if (impl_->content == content) {
        return;
    }
    if (impl_->content != nullptr) {
        remove_child(*impl_->content);
    }
    impl_->content = std::move(content);
    if (impl_->content != nullptr) {
        append_child(impl_->content);
    }
    queue_layout();
}

Widget* NavigationSplitView::content() const {
    return impl_->content.get();
}

bool NavigationSplitView::is_collapsed() const {
    return impl_->collapsed;
}

void NavigationSplitView::set_collapsed(bool collapsed) {
    if (impl_->collapsed == collapsed) {
        return;
    }
    impl_->collapsed = collapsed;
    impl_->collapsed_changed.emit(collapsed);
    queue_layout();
}

bool NavigationSplitView::shows_content() const {
    return impl_->show_content;
}

void NavigationSplitView::set_show_content(bool show_content) {
    if (impl_->show_content == show_content) {
        return;
    }
    impl_->show_content = show_content;
    impl_->show_content_changed.emit(show_content);
    queue_layout();
}

float NavigationSplitView::sidebar_width_fraction() const {
    return impl_->sidebar_width_fraction;
}

void NavigationSplitView::set_sidebar_width_fraction(float fraction) {
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    if (impl_->sidebar_width_fraction == fraction) {
        return;
    }
    impl_->sidebar_width_fraction = fraction;
    queue_layout();
}

void NavigationSplitView::set_min_sidebar_width(float width) {
    width = std::max(0.0F, width);
    if (impl_->min_sidebar_width != width) {
        impl_->min_sidebar_width = width;
        queue_layout();
    }
}

void NavigationSplitView::set_max_sidebar_width(float width) {
    width = std::max(0.0F, width);
    if (impl_->max_sidebar_width != width) {
        impl_->max_sidebar_width = width;
        queue_layout();
    }
}

Signal<bool>& NavigationSplitView::on_collapsed_changed() {
    return impl_->collapsed_changed;
}

Signal<bool>& NavigationSplitView::on_show_content_changed() {
    return impl_->show_content_changed;
}

SizeRequest NavigationSplitView::measure(const Constraints& constraints) const {
    const auto sidebar_request = impl_->sidebar != nullptr
                                     ? impl_->sidebar->measure_for_diagnostics(constraints)
                                     : SizeRequest{};
    const auto content_request = impl_->content != nullptr
                                     ? impl_->content->measure_for_diagnostics(constraints)
                                     : SizeRequest{};
    if (impl_->collapsed) {
        return {
            std::max(sidebar_request.minimum_width, content_request.minimum_width),
            std::max(sidebar_request.minimum_height, content_request.minimum_height),
            std::max(sidebar_request.natural_width, content_request.natural_width),
            std::max(sidebar_request.natural_height, content_request.natural_height),
        };
    }
    const float separator = theme_number("separator-width", 1.0F);
    return {
        sidebar_request.minimum_width + content_request.minimum_width + separator,
        std::max(sidebar_request.minimum_height, content_request.minimum_height),
        sidebar_request.natural_width + content_request.natural_width + separator,
        std::max(sidebar_request.natural_height, content_request.natural_height),
    };
}

void NavigationSplitView::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (impl_->collapsed) {
        if (impl_->sidebar != nullptr) {
            impl_->sidebar->set_visible(!impl_->show_content);
            if (!impl_->show_content) {
                impl_->sidebar->allocate(allocation);
                impl_->sidebar_bounds = allocation;
            }
        }
        if (impl_->content != nullptr) {
            impl_->content->set_visible(impl_->show_content);
            if (impl_->show_content) {
                impl_->content->allocate(allocation);
            }
        }
        return;
    }

    if (impl_->sidebar != nullptr) {
        impl_->sidebar->set_visible(true);
    }
    if (impl_->content != nullptr) {
        impl_->content->set_visible(true);
    }
    const float separator = theme_number("separator-width", 1.0F);
    const float available = std::max(0.0F, allocation.width - separator);
    const float upper = std::min(impl_->max_sidebar_width, available);
    const float lower = std::min({impl_->min_sidebar_width, upper, available});
    const float sidebar_width = std::clamp(available * impl_->sidebar_width_fraction, lower, upper);
    impl_->sidebar_bounds = {allocation.x, allocation.y, sidebar_width, allocation.height};
    if (impl_->sidebar != nullptr) {
        impl_->sidebar->allocate(impl_->sidebar_bounds);
    }
    if (impl_->content != nullptr) {
        impl_->content->allocate({allocation.x + sidebar_width + separator,
                                  allocation.y,
                                  std::max(0.0F, available - sidebar_width),
                                  allocation.height});
    }
}

void NavigationSplitView::snapshot(SnapshotContext& ctx) const {
    if (impl_->sidebar != nullptr && impl_->sidebar->is_visible()) {
        ctx.add_color_rect(impl_->sidebar_bounds,
                           theme_color("sidebar-background",
                                       theme_color("sidebar-bg", Color::from_rgb(246, 247, 249))));
        if (!impl_->collapsed) {
            ctx.add_color_rect(
                {impl_->sidebar_bounds.right() - 1.0F,
                 impl_->sidebar_bounds.y,
                 1.0F,
                 impl_->sidebar_bounds.height},
                theme_color("sidebar-border-color",
                            theme_color("sidebar-border", Color::from_rgb(224, 228, 234))));
        }
    }
    Widget::snapshot(ctx);
}

} // namespace nk
