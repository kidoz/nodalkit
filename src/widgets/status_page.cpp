#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/status_page.h>

namespace nk {

struct StatusPage::Impl {
    std::string title;
    std::string description;
    std::shared_ptr<Widget> icon;
    std::shared_ptr<Widget> action;
    Rect text_bounds{};
    float title_y = 0.0F;
    float description_y = 0.0F;
};

std::shared_ptr<StatusPage> StatusPage::create(std::string title, std::string description) {
    return std::shared_ptr<StatusPage>(new StatusPage(std::move(title), std::move(description)));
}

StatusPage::StatusPage(std::string title, std::string description)
    : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    impl_->description = std::move(description);
    add_style_class("status-page");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name(impl_->title);
    accessible.set_description(impl_->description);
}

StatusPage::~StatusPage() = default;

std::string_view StatusPage::title() const {
    return impl_->title;
}

void StatusPage::set_title(std::string title) {
    if (impl_->title != title) {
        impl_->title = std::move(title);
        ensure_accessible().set_name(impl_->title);
        queue_layout();
    }
}

std::string_view StatusPage::description() const {
    return impl_->description;
}

void StatusPage::set_description(std::string description) {
    if (impl_->description != description) {
        impl_->description = std::move(description);
        ensure_accessible().set_description(impl_->description);
        queue_layout();
    }
}

void StatusPage::set_icon(std::shared_ptr<Widget> icon) {
    if (impl_->icon == icon) {
        return;
    }
    if (impl_->icon != nullptr) {
        remove_child(*impl_->icon);
    }
    impl_->icon = std::move(icon);
    if (impl_->icon != nullptr) {
        append_child(impl_->icon);
    }
    queue_layout();
}

Widget* StatusPage::icon() const {
    return impl_->icon.get();
}

void StatusPage::set_action(std::shared_ptr<Widget> action) {
    if (impl_->action == action) {
        return;
    }
    if (impl_->action != nullptr) {
        remove_child(*impl_->action);
    }
    impl_->action = std::move(action);
    if (impl_->action != nullptr) {
        append_child(impl_->action);
    }
    queue_layout();
}

Widget* StatusPage::action() const {
    return impl_->action.get();
}

SizeRequest StatusPage::measure(const Constraints& constraints) const {
    const auto title_font = FontDescriptor{
        .family = {}, .size = theme_number("title-font-size", 20.0F), .weight = FontWeight::Bold};
    const auto body_font = FontDescriptor{
        .family = {}, .size = theme_number("body-font-size", 13.0F), .weight = FontWeight::Regular};
    const auto title = measure_text(impl_->title, title_font);
    const float content_width = theme_number("content-width", 420.0F);
    const auto description = measure_text_wrapped(impl_->description, body_font, content_width);
    const auto icon =
        impl_->icon != nullptr ? impl_->icon->measure_for_diagnostics(constraints) : SizeRequest{};
    const auto action = impl_->action != nullptr
                            ? impl_->action->measure_for_diagnostics(constraints)
                            : SizeRequest{};
    const float icon_spacing = impl_->icon != nullptr ? theme_number("icon-spacing", 16.0F) : 0.0F;
    const float title_spacing =
        !impl_->description.empty() ? theme_number("title-spacing", 8.0F) : 0.0F;
    const float action_spacing =
        impl_->action != nullptr ? theme_number("action-spacing", 24.0F) : 0.0F;
    const float width =
        std::max({title.width, description.width, icon.natural_width, action.natural_width});
    const float height = icon.natural_height + icon_spacing + title.height + title_spacing +
                         description.height + action_spacing + action.natural_height;
    return {std::min(width, constraints.max_width),
            height,
            std::min(width, constraints.max_width),
            height};
}

void StatusPage::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const auto title_font = FontDescriptor{
        .family = {}, .size = theme_number("title-font-size", 20.0F), .weight = FontWeight::Bold};
    const auto body_font = FontDescriptor{
        .family = {}, .size = theme_number("body-font-size", 13.0F), .weight = FontWeight::Regular};
    const float horizontal_padding = theme_number("horizontal-padding", 24.0F);
    const float width = std::min(theme_number("content-width", 420.0F),
                                 std::max(0.0F, allocation.width - (horizontal_padding * 2.0F)));
    const auto title = measure_text(impl_->title, title_font);
    const auto description = measure_text_wrapped(impl_->description, body_font, width);
    const auto icon = impl_->icon != nullptr
                          ? impl_->icon->measure({0.0F, 0.0F, width, allocation.height})
                          : SizeRequest{};
    const auto action = impl_->action != nullptr
                            ? impl_->action->measure({0.0F, 0.0F, width, allocation.height})
                            : SizeRequest{};
    const float icon_spacing = impl_->icon != nullptr ? theme_number("icon-spacing", 16.0F) : 0.0F;
    const float title_spacing =
        !impl_->description.empty() ? theme_number("title-spacing", 8.0F) : 0.0F;
    const float action_spacing =
        impl_->action != nullptr ? theme_number("action-spacing", 24.0F) : 0.0F;
    const float block_height = icon.natural_height + icon_spacing + title.height + title_spacing +
                               description.height + action_spacing + action.natural_height;
    const float x = allocation.x + ((allocation.width - width) * 0.5F);
    float y = allocation.y + std::max(0.0F, (allocation.height - block_height) * 0.5F);

    if (impl_->icon != nullptr) {
        impl_->icon->allocate({x + ((width - icon.natural_width) * 0.5F),
                               y,
                               icon.natural_width,
                               icon.natural_height});
        y += icon.natural_height + icon_spacing;
    }

    impl_->title_y = y;
    y += title.height + title_spacing;
    impl_->description_y = y;
    impl_->text_bounds = {
        x, impl_->title_y, width, title.height + title_spacing + description.height};
    y += description.height;
    if (impl_->action != nullptr) {
        y += action_spacing;
        impl_->action->allocate({x + ((width - action.natural_width) * 0.5F),
                                 y,
                                 action.natural_width,
                                 action.natural_height});
    }
}

void StatusPage::snapshot(SnapshotContext& ctx) const {
    const auto title_font = FontDescriptor{
        .family = {}, .size = theme_number("title-font-size", 20.0F), .weight = FontWeight::Bold};
    const auto body_font = FontDescriptor{
        .family = {}, .size = theme_number("body-font-size", 13.0F), .weight = FontWeight::Regular};
    const auto title = measure_text(impl_->title, title_font);
    ctx.add_text(
        {impl_->text_bounds.x + ((impl_->text_bounds.width - title.width) * 0.5F), impl_->title_y},
        impl_->title,
        theme_color("text-color"),
        title_font);
    ctx.add_wrapped_text({impl_->text_bounds.x, impl_->description_y},
                         impl_->description,
                         theme_color("description-color",
                                     theme_color("text-secondary", Color::from_rgb(96, 103, 114))),
                         body_font,
                         impl_->text_bounds.width);
    Widget::snapshot(ctx);
}

} // namespace nk
