#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/toolbar_view.h>

namespace nk {

namespace {

class ToolbarBackdrop final : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ToolbarBackdrop> create() {
        return std::shared_ptr<ToolbarBackdrop>(new ToolbarBackdrop());
    }

    void set_regions(Rect top, Rect bottom, ToolbarStyle top_style, ToolbarStyle bottom_style) {
        top_ = top;
        bottom_ = bottom;
        top_style_ = top_style;
        bottom_style_ = bottom_style;
    }

    [[nodiscard]] bool hit_test(Point /*point*/) const override { return false; }

protected:
    void snapshot(SnapshotContext& ctx) const override {
        const bool focused = host_window() == nullptr || host_window()->is_focused();
        const auto background =
            focused
                ? theme_color("bar-background",
                              theme_color("headerbar-bg", Color::from_rgb(253, 253, 254)))
                : theme_color("bar-backdrop-background",
                              theme_color("headerbar-backdrop", Color::from_rgb(248, 249, 252)));
        const auto border = theme_color(
            "bar-border-color", theme_color("headerbar-shade", Color::from_rgb(224, 228, 234)));
        const float separator = theme_number("separator-width", 1.0F);
        if (top_.height > 0.0F) {
            ctx.add_color_rect(top_, background);
            if (top_style_ != ToolbarStyle::Flat) {
                const float width = top_style_ == ToolbarStyle::RaisedBorder ? 2.0F : separator;
                ctx.add_color_rect({top_.x, top_.bottom() - width, top_.width, width}, border);
            }
        }
        if (bottom_.height > 0.0F) {
            ctx.add_color_rect(bottom_, background);
            if (bottom_style_ != ToolbarStyle::Flat) {
                const float width = bottom_style_ == ToolbarStyle::RaisedBorder ? 2.0F : separator;
                ctx.add_color_rect({bottom_.x, bottom_.y, bottom_.width, width}, border);
            }
        }
    }

private:
    ToolbarBackdrop() {
        add_style_class("toolbar-view-backdrop");
        ensure_accessible().set_hidden(true);
    }

    Rect top_{};
    Rect bottom_{};
    ToolbarStyle top_style_ = ToolbarStyle::Flat;
    ToolbarStyle bottom_style_ = ToolbarStyle::Flat;
};

} // namespace

struct ToolbarView::Impl {
    std::shared_ptr<Widget> content;
    std::shared_ptr<ToolbarBackdrop> backdrop;
    std::vector<std::shared_ptr<Widget>> top_bars;
    std::vector<std::shared_ptr<Widget>> bottom_bars;
    ToolbarStyle top_style = ToolbarStyle::Flat;
    ToolbarStyle bottom_style = ToolbarStyle::Flat;
    Rect top_bounds{};
    Rect bottom_bounds{};
    float top_height = 0.0F;
    float bottom_height = 0.0F;
    bool reveal_top = true;
    bool reveal_bottom = true;
    bool extend_top = false;
    bool extend_bottom = false;
};

std::shared_ptr<ToolbarView> ToolbarView::create() {
    return std::shared_ptr<ToolbarView>(new ToolbarView());
}

ToolbarView::ToolbarView() : impl_(std::make_unique<Impl>()) {
    impl_->backdrop = ToolbarBackdrop::create();
    append_child(impl_->backdrop);
    add_style_class("toolbar-view");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name("Toolbar View");
}

ToolbarView::~ToolbarView() = default;

void ToolbarView::set_content(std::shared_ptr<Widget> content) {
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

Widget* ToolbarView::content() const {
    return impl_->content.get();
}

void ToolbarView::add_top_bar(std::shared_ptr<Widget> bar) {
    if (bar == nullptr ||
        std::find(impl_->top_bars.begin(), impl_->top_bars.end(), bar) != impl_->top_bars.end()) {
        return;
    }
    impl_->top_bars.push_back(bar);
    insert_child((impl_->content != nullptr ? 2U : 1U) + impl_->top_bars.size() - 1U,
                 std::move(bar));
    queue_layout();
}

void ToolbarView::add_bottom_bar(std::shared_ptr<Widget> bar) {
    if (bar == nullptr || std::find(impl_->bottom_bars.begin(), impl_->bottom_bars.end(), bar) !=
                              impl_->bottom_bars.end()) {
        return;
    }
    impl_->bottom_bars.push_back(bar);
    append_child(std::move(bar));
    queue_layout();
}

void ToolbarView::remove(Widget& bar) {
    auto erase = [&](auto& bars) {
        const auto iterator = std::find_if(
            bars.begin(), bars.end(), [&](const auto& item) { return item.get() == &bar; });
        if (iterator == bars.end()) {
            return false;
        }
        remove_child(**iterator);
        bars.erase(iterator);
        return true;
    };
    if (erase(impl_->top_bars) || erase(impl_->bottom_bars)) {
        queue_layout();
    }
}

std::span<const std::shared_ptr<Widget>> ToolbarView::top_bars() const {
    return impl_->top_bars;
}

std::span<const std::shared_ptr<Widget>> ToolbarView::bottom_bars() const {
    return impl_->bottom_bars;
}

ToolbarStyle ToolbarView::top_bar_style() const {
    return impl_->top_style;
}

void ToolbarView::set_top_bar_style(ToolbarStyle style) {
    if (impl_->top_style != style) {
        impl_->top_style = style;
        queue_redraw();
    }
}

ToolbarStyle ToolbarView::bottom_bar_style() const {
    return impl_->bottom_style;
}

void ToolbarView::set_bottom_bar_style(ToolbarStyle style) {
    if (impl_->bottom_style != style) {
        impl_->bottom_style = style;
        queue_redraw();
    }
}

bool ToolbarView::reveals_top_bars() const {
    return impl_->reveal_top;
}

void ToolbarView::set_reveal_top_bars(bool reveal) {
    if (impl_->reveal_top != reveal) {
        impl_->reveal_top = reveal;
        queue_layout();
    }
}

bool ToolbarView::reveals_bottom_bars() const {
    return impl_->reveal_bottom;
}

void ToolbarView::set_reveal_bottom_bars(bool reveal) {
    if (impl_->reveal_bottom != reveal) {
        impl_->reveal_bottom = reveal;
        queue_layout();
    }
}

bool ToolbarView::extends_content_to_top_edge() const {
    return impl_->extend_top;
}

void ToolbarView::set_extend_content_to_top_edge(bool extend) {
    if (impl_->extend_top != extend) {
        impl_->extend_top = extend;
        queue_layout();
    }
}

bool ToolbarView::extends_content_to_bottom_edge() const {
    return impl_->extend_bottom;
}

void ToolbarView::set_extend_content_to_bottom_edge(bool extend) {
    if (impl_->extend_bottom != extend) {
        impl_->extend_bottom = extend;
        queue_layout();
    }
}

float ToolbarView::top_bar_height() const {
    return impl_->top_height;
}

float ToolbarView::bottom_bar_height() const {
    return impl_->bottom_height;
}

SizeRequest ToolbarView::measure(const Constraints& constraints) const {
    SizeRequest result = impl_->content != nullptr
                             ? impl_->content->measure_for_diagnostics(constraints)
                             : SizeRequest{};
    auto accumulate = [&](const auto& bars, bool revealed) {
        if (!revealed) {
            return SizeRequest{};
        }
        SizeRequest bars_request{};
        for (const auto& bar : bars) {
            const auto request = bar->measure_for_diagnostics(constraints);
            bars_request.minimum_width =
                std::max(bars_request.minimum_width, request.minimum_width);
            bars_request.natural_width =
                std::max(bars_request.natural_width, request.natural_width);
            bars_request.minimum_height += request.minimum_height;
            bars_request.natural_height += request.natural_height;
        }
        return bars_request;
    };
    const auto top = accumulate(impl_->top_bars, impl_->reveal_top);
    const auto bottom = accumulate(impl_->bottom_bars, impl_->reveal_bottom);
    result.minimum_width =
        std::max({result.minimum_width, top.minimum_width, bottom.minimum_width});
    result.natural_width =
        std::max({result.natural_width, top.natural_width, bottom.natural_width});
    if (!impl_->extend_top) {
        result.minimum_height += top.minimum_height;
        result.natural_height += top.natural_height;
    }
    if (!impl_->extend_bottom) {
        result.minimum_height += bottom.minimum_height;
        result.natural_height += bottom.natural_height;
    }
    return result;
}

void ToolbarView::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    impl_->top_height = 0.0F;
    impl_->bottom_height = 0.0F;
    const Constraints bar_constraints{0.0F, 0.0F, allocation.width, allocation.height};

    float top_y = allocation.y;
    for (const auto& bar : impl_->top_bars) {
        bar->set_visible(impl_->reveal_top);
        if (!impl_->reveal_top) {
            continue;
        }
        const float height = bar->measure(bar_constraints).natural_height;
        bar->allocate({allocation.x, top_y, allocation.width, height});
        top_y += height;
        impl_->top_height += height;
    }
    impl_->top_bounds = {allocation.x, allocation.y, allocation.width, impl_->top_height};

    float bottom_y = allocation.bottom();
    for (auto iterator = impl_->bottom_bars.rbegin(); iterator != impl_->bottom_bars.rend();
         ++iterator) {
        (*iterator)->set_visible(impl_->reveal_bottom);
        if (!impl_->reveal_bottom) {
            continue;
        }
        const float height = (*iterator)->measure(bar_constraints).natural_height;
        bottom_y -= height;
        (*iterator)->allocate({allocation.x, bottom_y, allocation.width, height});
        impl_->bottom_height += height;
    }
    impl_->bottom_bounds = {allocation.x,
                            allocation.bottom() - impl_->bottom_height,
                            allocation.width,
                            impl_->bottom_height};
    impl_->backdrop->allocate(allocation);
    impl_->backdrop->set_regions(impl_->reveal_top ? impl_->top_bounds : Rect{},
                                 impl_->reveal_bottom ? impl_->bottom_bounds : Rect{},
                                 impl_->top_style,
                                 impl_->bottom_style);

    if (impl_->content != nullptr) {
        const float content_y = impl_->extend_top ? allocation.y : top_y;
        const float content_bottom = impl_->extend_bottom ? allocation.bottom() : bottom_y;
        impl_->content->allocate({allocation.x,
                                  content_y,
                                  allocation.width,
                                  std::max(0.0F, content_bottom - content_y)});
    }
}

bool ToolbarView::handle_mouse_event(const MouseEvent& event) {
    if (event.type != MouseEvent::Type::Press || event.button != 1) {
        return false;
    }
    if (!impl_->top_bounds.contains({event.x, event.y}) &&
        !impl_->bottom_bounds.contains({event.x, event.y})) {
        return false;
    }
    if (auto* window = host_window(); window != nullptr) {
        if (event.click_count >= 2) {
            window->toggle_maximize();
            return true;
        }
        return window->begin_system_move(event.native_serial);
    }
    return false;
}

void ToolbarView::snapshot(SnapshotContext& ctx) const {
    Widget::snapshot(ctx);
}

} // namespace nk
