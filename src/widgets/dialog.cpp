#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/dialog.h>

namespace nk {

namespace {

FontDescriptor dialog_title_font() {
    return FontDescriptor{
        .family = {},
        .size = 16.0F,
        .weight = FontWeight::Medium,
    };
}

FontDescriptor dialog_body_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

FontDescriptor dialog_button_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

} // namespace

struct Dialog::Impl {
    std::string title;
    std::string message;
    std::shared_ptr<Widget> content;
    Window* parent_window = nullptr;
    bool presented = false;
    mutable bool backdrop_dirty = false;
    int armed_button = -1;
    DialogPresentationStyle presentation_style = DialogPresentationStyle::Default;
    float minimum_panel_width = 280.0F;
    Rect panel_bounds{};
    Rect previous_panel_bounds{};
    bool has_previous_panel_bounds = false;
    std::vector<Rect> button_bounds;

    struct ButtonEntry {
        std::string label;
        DialogResponse response;
    };

    std::vector<ButtonEntry> buttons;

    Signal<DialogResponse> on_response;
};

std::shared_ptr<Dialog> Dialog::create(std::string title, std::string message) {
    return std::shared_ptr<Dialog>(new Dialog(std::move(title), std::move(message)));
}

Dialog::Dialog(std::string title, std::string message) : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    impl_->message = std::move(message);
    add_style_class("dialog");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Dialog);
    accessible.set_name(impl_->title);
    accessible.set_description(impl_->message);
}

Dialog::~Dialog() = default;

void Dialog::add_button(std::string label, DialogResponse response) {
    impl_->buttons.push_back({std::move(label), response});
    queue_layout();
    queue_redraw();
}

void Dialog::set_content(std::shared_ptr<Widget> content) {
    if (impl_->content) {
        remove_child(*impl_->content);
    }
    impl_->content = std::move(content);
    if (impl_->content) {
        append_child(impl_->content);
    }
    queue_layout();
    queue_redraw();
}

void Dialog::set_presentation_style(DialogPresentationStyle style) {
    if (impl_->presentation_style == style) {
        return;
    }
    impl_->presentation_style = style;
    queue_layout();
    queue_redraw();
}

void Dialog::set_minimum_panel_width(float width) {
    width = std::max(120.0F, width);
    if (impl_->minimum_panel_width == width) {
        return;
    }
    impl_->minimum_panel_width = width;
    queue_layout();
    queue_redraw();
}

void Dialog::present(Window& parent) {
    if (impl_->presented && impl_->parent_window == &parent) {
        return;
    }

    if (impl_->presented && impl_->parent_window != nullptr) {
        impl_->parent_window->dismiss_overlay(*this);
    }

    impl_->parent_window = &parent;
    impl_->presented = true;
    impl_->backdrop_dirty = true;
    impl_->armed_button = -1;
    parent.show_overlay(shared_from_this(), true);
}

bool Dialog::is_presented() const {
    return impl_->presented;
}

void Dialog::close(DialogResponse response) {
    if (impl_->presented && impl_->parent_window != nullptr) {
        impl_->backdrop_dirty = true;
        impl_->parent_window->dismiss_overlay(*this);
    }
    impl_->parent_window = nullptr;
    impl_->presented = false;
    impl_->armed_button = -1;
    impl_->on_response.emit(response);
}

Signal<DialogResponse>& Dialog::on_response() {
    return impl_->on_response;
}

SizeRequest Dialog::measure(const Constraints& /*constraints*/) const {
    constexpr float padding = 20.0F;
    constexpr float spacing = 12.0F;
    constexpr float min_height = 140.0F;
    const float min_width = impl_->minimum_panel_width;

    const auto title_size = measure_text(impl_->title, dialog_title_font());
    const auto message_size =
        impl_->message.empty() ? Size{} : measure_text(impl_->message, dialog_body_font());

    float buttons_width = 0.0F;
    float buttons_height = 36.0F;
    for (std::size_t index = 0; index < impl_->buttons.size(); ++index) {
        const auto label_size = measure_text(impl_->buttons[index].label, dialog_button_font());
        buttons_width += std::max(88.0F, label_size.width + 32.0F);
        if (index + 1 < impl_->buttons.size()) {
            buttons_width += 8.0F;
        }
    }

    SizeRequest content_req{};
    if (impl_->content) {
        content_req = impl_->content->measure_for_diagnostics(Constraints::unbounded());
    }

    const float natural_width = std::max({min_width,
                                          title_size.width + (padding * 2.0F),
                                          message_size.width + (padding * 2.0F),
                                          content_req.natural_width + (padding * 2.0F),
                                          buttons_width + (padding * 2.0F)});
    float natural_height = padding + title_size.height;
    if (!impl_->message.empty() || impl_->content) {
        natural_height += spacing;
    }
    natural_height += impl_->content ? content_req.natural_height : message_size.height;
    if (!impl_->buttons.empty()) {
        natural_height += spacing + buttons_height;
    }
    natural_height += padding;

    return {min_width, min_height, natural_width, std::max(min_height, natural_height)};
}

void Dialog::allocate(const Rect& allocation) {
    Widget::allocate(allocation);

    constexpr float margin = 24.0F;
    constexpr float padding = 20.0F;
    constexpr float spacing = 12.0F;
    constexpr float button_height = 36.0F;

    const auto preferred = measure(Constraints::tight(allocation.size()));
    const float panel_width = std::clamp(preferred.natural_width,
                                         std::min(preferred.minimum_width, allocation.width),
                                         std::max(0.0F, allocation.width - (margin * 2.0F)));
    float panel_height = std::clamp(preferred.natural_height,
                                    std::min(preferred.minimum_height, allocation.height),
                                    std::max(0.0F, allocation.height - (margin * 2.0F)));

    const float panel_x = allocation.x + std::max(0.0F, (allocation.width - panel_width) * 0.5F);
    float panel_y = allocation.y + std::max(0.0F, (allocation.height - panel_height) * 0.5F);
    if (impl_->presentation_style == DialogPresentationStyle::Sheet) {
        panel_y = allocation.y + margin;
    }

    const Rect next_panel_bounds{panel_x, panel_y, panel_width, panel_height};
    if (impl_->panel_bounds != next_panel_bounds && impl_->panel_bounds.width > 0.0F &&
        impl_->panel_bounds.height > 0.0F) {
        impl_->previous_panel_bounds = impl_->panel_bounds;
        impl_->has_previous_panel_bounds = true;
    }
    impl_->panel_bounds = next_panel_bounds;

    impl_->button_bounds.clear();
    float inner_x = impl_->panel_bounds.x + padding;
    float inner_width = std::max(0.0F, impl_->panel_bounds.width - (padding * 2.0F));
    float current_y = impl_->panel_bounds.y + padding;

    const auto title_size = measure_text(impl_->title, dialog_title_font());
    current_y += title_size.height;
    if (!impl_->message.empty() || impl_->content) {
        current_y += spacing;
    }

    float buttons_total_width = 0.0F;
    std::vector<float> button_widths;
    button_widths.reserve(impl_->buttons.size());
    for (std::size_t index = 0; index < impl_->buttons.size(); ++index) {
        const auto label_size = measure_text(impl_->buttons[index].label, dialog_button_font());
        const float button_width = std::max(88.0F, label_size.width + 32.0F);
        button_widths.push_back(button_width);
        buttons_total_width += button_width;
        if (index + 1 < impl_->buttons.size()) {
            buttons_total_width += 8.0F;
        }
    }

    float content_bottom = impl_->panel_bounds.bottom() - padding;
    if (!impl_->buttons.empty()) {
        content_bottom -= button_height + spacing;
    }
    float content_height = std::max(0.0F, content_bottom - current_y);

    if (impl_->content) {
        impl_->content->allocate({inner_x, current_y, inner_width, content_height});
    }

    if (!impl_->buttons.empty()) {
        float button_x = impl_->panel_bounds.right() - padding - buttons_total_width;
        float button_y = impl_->panel_bounds.bottom() - padding - button_height;
        for (float button_width : button_widths) {
            impl_->button_bounds.push_back({button_x, button_y, button_width, button_height});
            button_x += button_width + 8.0F;
        }
    }
}

bool Dialog::handle_mouse_event(const MouseEvent& event) {
    if (!impl_->presented) {
        return false;
    }

    const auto point = Point{event.x, event.y};
    if (event.button != 1 && event.type != MouseEvent::Type::Move &&
        event.type != MouseEvent::Type::Leave) {
        return allocation().contains(point);
    }

    auto button_at = [this, point]() -> int {
        for (std::size_t index = 0; index < impl_->button_bounds.size(); ++index) {
            if (impl_->button_bounds[index].contains(point)) {
                return static_cast<int>(index);
            }
        }
        return -1;
    };

    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed_button = button_at();
        return allocation().contains(point);
    case MouseEvent::Type::Release: {
        const int released_button = button_at();
        const int activated_button = impl_->armed_button;
        impl_->armed_button = -1;
        if (activated_button >= 0 && activated_button == released_button &&
            activated_button < static_cast<int>(impl_->buttons.size())) {
            close(impl_->buttons[static_cast<std::size_t>(activated_button)].response);
        }
        return allocation().contains(point);
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return allocation().contains(point);
    }

    return false;
}

bool Dialog::handle_key_event(const KeyEvent& event) {
    if (!impl_->presented || event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Escape) {
        auto cancel = std::find_if(
            impl_->buttons.begin(), impl_->buttons.end(), [](const Impl::ButtonEntry& button) {
                return button.response == DialogResponse::Cancel;
            });
        close(cancel != impl_->buttons.end() ? cancel->response : DialogResponse::Close);
        return true;
    }

    if (event.key == KeyCode::Return || event.key == KeyCode::Space) {
        auto accept = std::find_if(
            impl_->buttons.begin(), impl_->buttons.end(), [](const Impl::ButtonEntry& button) {
                return button.response == DialogResponse::Accept;
            });
        if (accept != impl_->buttons.end()) {
            close(accept->response);
            return true;
        }
        if (!impl_->buttons.empty()) {
            close(impl_->buttons.front().response);
            return true;
        }
        close(DialogResponse::Close);
        return true;
    }

    return false;
}

bool Dialog::hit_test(Point point) const {
    return impl_->presented && allocation().contains(point);
}

std::vector<Rect> Dialog::damage_regions() const {
    if (!impl_->presented) {
        return {};
    }

    if (impl_->backdrop_dirty) {
        return {allocation()};
    }

    constexpr float shadow_offset = 2.0F;
    if (impl_->panel_bounds.width <= 0.0F || impl_->panel_bounds.height <= 0.0F) {
        return {allocation()};
    }

    std::vector<Rect> regions;
    regions.push_back({
        impl_->panel_bounds.x,
        impl_->panel_bounds.y,
        impl_->panel_bounds.width,
        impl_->panel_bounds.height + shadow_offset,
    });
    if (impl_->has_previous_panel_bounds && impl_->previous_panel_bounds.width > 0.0F &&
        impl_->previous_panel_bounds.height > 0.0F) {
        regions.push_back({
            impl_->previous_panel_bounds.x,
            impl_->previous_panel_bounds.y,
            impl_->previous_panel_bounds.width,
            impl_->previous_panel_bounds.height + shadow_offset,
        });
    }
    return regions;
}

void Dialog::snapshot(SnapshotContext& ctx) const {
    if (!impl_->presented) {
        return;
    }

    constexpr float padding = 20.0F;
    constexpr float spacing = 12.0F;
    constexpr float corner_radius = 16.0F;
    constexpr float button_radius = 10.0F;

    ctx.push_overlay_container(allocation());
    ctx.add_color_rect(allocation(), Color{0.07F, 0.09F, 0.12F, 0.28F});
    ctx.add_rounded_rect({impl_->panel_bounds.x,
                          impl_->panel_bounds.y + 2.0F,
                          impl_->panel_bounds.width,
                          impl_->panel_bounds.height},
                         Color{0.08F, 0.12F, 0.18F, 0.08F},
                         corner_radius + 1.0F);
    ctx.add_rounded_rect(impl_->panel_bounds,
                         theme_color("dialog-background", Color{0.98F, 0.98F, 0.99F, 1.0F}),
                         corner_radius);
    ctx.add_border(impl_->panel_bounds,
                   theme_color("dialog-border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}),
                   1.0F,
                   corner_radius);

    const auto title_font = dialog_title_font();
    const auto body_font = dialog_body_font();
    const auto button_font = dialog_button_font();
    const auto title_size = measure_text(impl_->title, title_font);
    const float inner_x = impl_->panel_bounds.x + padding;
    float current_y = impl_->panel_bounds.y + padding;
    ctx.add_text({inner_x, current_y},
                 impl_->title,
                 theme_color("dialog-title-color", Color{0.1F, 0.1F, 0.12F, 1.0F}),
                 title_font);
    current_y += title_size.height;

    if (!impl_->message.empty()) {
        current_y += spacing;
        const auto message_size = measure_text(impl_->message, body_font);
        ctx.add_text({inner_x, current_y},
                     impl_->message,
                     theme_color("dialog-text-color", Color{0.26F, 0.29F, 0.33F, 1.0F}),
                     body_font);
        current_y += message_size.height;
    }

    if (impl_->content) {
        Widget::snapshot(ctx);
    }

    for (std::size_t index = 0; index < impl_->button_bounds.size(); ++index) {
        const auto& button = impl_->buttons[index];
        const auto& bounds = impl_->button_bounds[index];
        ctx.add_rounded_rect(
            bounds,
            theme_color("dialog-button-background", Color{0.94F, 0.95F, 0.97F, 1.0F}),
            button_radius);
        ctx.add_border(bounds,
                       theme_color("dialog-button-border", Color{0.8F, 0.82F, 0.86F, 1.0F}),
                       1.0F,
                       button_radius);

        const auto label_size = measure_text(button.label, button_font);
        const float text_x = bounds.x + std::max(0.0F, (bounds.width - label_size.width) * 0.5F);
        const float text_y = bounds.y + std::max(0.0F, (bounds.height - label_size.height) * 0.5F);
        ctx.add_text({text_x, text_y},
                     button.label,
                     theme_color("dialog-button-text", Color{0.1F, 0.1F, 0.12F, 1.0F}),
                     button_font);
    }
    ctx.pop_container();
    impl_->backdrop_dirty = false;
    impl_->has_previous_panel_bounds = false;
}

} // namespace nk
