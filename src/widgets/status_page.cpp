#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/status_page.h>

namespace nk {

struct StatusPage::Impl {
    std::string title;
    std::string description;
    std::shared_ptr<Widget> action;
    Rect text_bounds{};
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
    const auto description = measure_text_wrapped(impl_->description, body_font, 420.0F);
    const auto action = impl_->action != nullptr
                            ? impl_->action->measure_for_diagnostics(constraints)
                            : SizeRequest{};
    const float width = std::max({title.width, description.width, action.natural_width});
    const float height = title.height + description.height + action.natural_height + 40.0F;
    return {std::min(width, constraints.max_width),
            height,
            std::min(width, constraints.max_width),
            height};
}

void StatusPage::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const float width = std::min(420.0F, std::max(0.0F, allocation.width - 48.0F));
    const float block_height = std::min(220.0F, allocation.height);
    impl_->text_bounds = {allocation.x + ((allocation.width - width) * 0.5F),
                          allocation.y + ((allocation.height - block_height) * 0.5F),
                          width,
                          block_height};
    if (impl_->action != nullptr) {
        const auto request = impl_->action->measure({0.0F, 0.0F, width, block_height});
        impl_->action->allocate({impl_->text_bounds.x + ((width - request.natural_width) * 0.5F),
                                 impl_->text_bounds.bottom() - request.natural_height,
                                 request.natural_width,
                                 request.natural_height});
    }
}

void StatusPage::snapshot(SnapshotContext& ctx) const {
    const auto title_font = FontDescriptor{
        .family = {}, .size = theme_number("title-font-size", 20.0F), .weight = FontWeight::Bold};
    const auto body_font = FontDescriptor{
        .family = {}, .size = theme_number("body-font-size", 13.0F), .weight = FontWeight::Regular};
    const auto title = measure_text(impl_->title, title_font);
    ctx.add_text({impl_->text_bounds.x + ((impl_->text_bounds.width - title.width) * 0.5F),
                  impl_->text_bounds.y},
                 impl_->title,
                 theme_color("text-color"),
                 title_font);
    const float body_y = impl_->text_bounds.y + title.height + 12.0F;
    ctx.add_wrapped_text({impl_->text_bounds.x, body_y},
                         impl_->description,
                         theme_color("description-color",
                                     theme_color("text-secondary", Color::from_rgb(96, 103, 114))),
                         body_font,
                         impl_->text_bounds.width);
    Widget::snapshot(ctx);
}

} // namespace nk
