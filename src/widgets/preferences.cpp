#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/preferences.h>

namespace nk {

namespace {

FontDescriptor preference_font(float size, FontWeight weight = FontWeight::Regular) {
    return {.family = {}, .size = size, .weight = weight};
}

} // namespace

struct PreferencesRow::Impl {
    std::string title;
    std::string subtitle;
    std::shared_ptr<Widget> suffix;
    Signal<> activated;
    bool activatable = false;
    bool armed = false;
};

std::shared_ptr<PreferencesRow> PreferencesRow::create(std::string title, std::string subtitle) {
    return std::shared_ptr<PreferencesRow>(
        new PreferencesRow(std::move(title), std::move(subtitle)));
}

PreferencesRow::PreferencesRow(std::string title, std::string subtitle)
    : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    impl_->subtitle = std::move(subtitle);
    add_style_class("preferences-row");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name(impl_->title);
    accessible.set_description(impl_->subtitle);
}

PreferencesRow::~PreferencesRow() = default;

std::string_view PreferencesRow::title() const {
    return impl_->title;
}

void PreferencesRow::set_title(std::string title) {
    if (impl_->title != title) {
        impl_->title = std::move(title);
        ensure_accessible().set_name(impl_->title);
        queue_layout();
    }
}

std::string_view PreferencesRow::subtitle() const {
    return impl_->subtitle;
}

void PreferencesRow::set_subtitle(std::string subtitle) {
    if (impl_->subtitle != subtitle) {
        impl_->subtitle = std::move(subtitle);
        ensure_accessible().set_description(impl_->subtitle);
        queue_layout();
    }
}

void PreferencesRow::set_suffix(std::shared_ptr<Widget> suffix) {
    if (impl_->suffix == suffix) {
        return;
    }
    if (impl_->suffix != nullptr) {
        remove_child(*impl_->suffix);
    }
    impl_->suffix = std::move(suffix);
    if (impl_->suffix != nullptr) {
        append_child(impl_->suffix);
    }
    queue_layout();
}

Widget* PreferencesRow::suffix() const {
    return impl_->suffix.get();
}

bool PreferencesRow::is_activatable() const {
    return impl_->activatable;
}

void PreferencesRow::set_activatable(bool activatable) {
    if (impl_->activatable == activatable) {
        return;
    }
    impl_->activatable = activatable;
    set_focusable(activatable);
    auto& accessible = ensure_accessible();
    accessible.set_role(activatable ? AccessibleRole::Button : AccessibleRole::Group);
    if (activatable) {
        accessible.add_action(AccessibleAction::Activate, [this] {
            impl_->activated.emit();
            return true;
        });
    } else {
        accessible.remove_action(AccessibleAction::Activate);
    }
    queue_redraw();
}

Signal<>& PreferencesRow::on_activated() {
    return impl_->activated;
}

SizeRequest PreferencesRow::measure(const Constraints& constraints) const {
    const float padding = theme_number("padding-x", 16.0F);
    const float min_height = theme_number("min-height", impl_->subtitle.empty() ? 50.0F : 66.0F);
    const auto title = measure_text(
        impl_->title, preference_font(theme_number("title-font-size", 13.0F), FontWeight::Medium));
    const auto subtitle =
        measure_text(impl_->subtitle, preference_font(theme_number("subtitle-font-size", 12.0F)));
    auto suffix = impl_->suffix != nullptr ? impl_->suffix->measure_for_diagnostics(constraints)
                                           : SizeRequest{};
    const float width = (padding * 2.0F) + std::max(title.width, subtitle.width) +
                        (impl_->suffix != nullptr ? suffix.natural_width + padding : 0.0F);
    return {std::min(width, constraints.max_width),
            min_height,
            std::min(width, constraints.max_width),
            std::max(min_height, suffix.natural_height)};
}

void PreferencesRow::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (impl_->suffix == nullptr) {
        return;
    }
    const float padding = theme_number("padding-x", 16.0F);
    const auto request = impl_->suffix->measure({0.0F, 0.0F, allocation.width, allocation.height});
    const float width = std::min(request.natural_width, std::max(0.0F, allocation.width - padding));
    const float height = std::min(request.natural_height, allocation.height);
    impl_->suffix->allocate({allocation.right() - padding - width,
                             allocation.y + ((allocation.height - height) * 0.5F),
                             width,
                             height});
}

bool PreferencesRow::handle_mouse_event(const MouseEvent& event) {
    if (!impl_->activatable || event.button != 1) {
        return false;
    }
    if (event.type == MouseEvent::Type::Press) {
        impl_->armed = allocation().contains({event.x, event.y});
        return impl_->armed;
    }
    if (event.type == MouseEvent::Type::Release) {
        const bool activate = impl_->armed && allocation().contains({event.x, event.y});
        impl_->armed = false;
        if (activate) {
            impl_->activated.emit();
        }
        return activate;
    }
    return false;
}

bool PreferencesRow::handle_key_event(const KeyEvent& event) {
    if (impl_->activatable && event.type == KeyEvent::Type::Press &&
        (event.key == KeyCode::Space || event.key == KeyCode::Return)) {
        impl_->activated.emit();
        return true;
    }
    return false;
}

void PreferencesRow::snapshot(SnapshotContext& ctx) const {
    const auto bounds = allocation();
    const float radius = theme_number("corner-radius", 12.0F);
    auto background =
        theme_color("background", theme_color("card-bg", Color::from_rgb(253, 253, 254)));
    if (impl_->activatable && has_flag(state_flags(), StateFlags::Hovered)) {
        background = theme_color("hover-background", Color::from_rgb(240, 243, 247));
    }
    ctx.add_rounded_rect(bounds, background, radius);
    ctx.add_border(
        bounds, theme_color("border-color", Color::from_rgb(224, 228, 234)), 1.0F, radius);
    const float padding = theme_number("padding-x", 16.0F);
    const auto title_font =
        preference_font(theme_number("title-font-size", 13.0F), FontWeight::Medium);
    const auto subtitle_font = preference_font(theme_number("subtitle-font-size", 12.0F));
    const auto title_size = measure_text(impl_->title, title_font);
    const float text_height =
        title_size.height + (impl_->subtitle.empty() ? 0.0F : 4.0F) +
        (impl_->subtitle.empty() ? 0.0F : measure_text(impl_->subtitle, subtitle_font).height);
    float y = bounds.y + ((bounds.height - text_height) * 0.5F);
    const float suffix_left = impl_->suffix != nullptr ? impl_->suffix->allocation().x - padding
                                                       : bounds.right() - padding;
    add_text_elided(ctx,
                    {bounds.x + padding, y},
                    impl_->title,
                    std::max(0.0F, suffix_left - bounds.x - (padding * 2.0F)),
                    theme_color("text-color"),
                    title_font);
    if (!impl_->subtitle.empty()) {
        y += title_size.height + 4.0F;
        add_text_elided(ctx,
                        {bounds.x + padding, y},
                        impl_->subtitle,
                        std::max(0.0F, suffix_left - bounds.x - (padding * 2.0F)),
                        theme_color("subtitle-color",
                                    theme_color("text-secondary", Color::from_rgb(96, 103, 114))),
                        subtitle_font);
    }
    Widget::snapshot(ctx);
}

struct PreferencesGroup::Impl {
    std::string title;
    std::vector<std::shared_ptr<PreferencesRow>> rows;
};

std::shared_ptr<PreferencesGroup> PreferencesGroup::create(std::string title) {
    return std::shared_ptr<PreferencesGroup>(new PreferencesGroup(std::move(title)));
}

PreferencesGroup::PreferencesGroup(std::string title) : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    add_style_class("preferences-group");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name(impl_->title.empty() ? "Preferences Group" : impl_->title);
}

PreferencesGroup::~PreferencesGroup() = default;

std::string_view PreferencesGroup::title() const {
    return impl_->title;
}

void PreferencesGroup::set_title(std::string title) {
    if (impl_->title != title) {
        impl_->title = std::move(title);
        ensure_accessible().set_name(impl_->title.empty() ? "Preferences Group" : impl_->title);
        queue_layout();
    }
}

void PreferencesGroup::add(std::shared_ptr<PreferencesRow> row) {
    if (row == nullptr ||
        std::find(impl_->rows.begin(), impl_->rows.end(), row) != impl_->rows.end()) {
        return;
    }
    impl_->rows.push_back(row);
    append_child(std::move(row));
    queue_layout();
}

void PreferencesGroup::remove(PreferencesRow& row) {
    const auto iterator = std::find_if(impl_->rows.begin(),
                                       impl_->rows.end(),
                                       [&](const auto& item) { return item.get() == &row; });
    if (iterator != impl_->rows.end()) {
        remove_child(**iterator);
        impl_->rows.erase(iterator);
        queue_layout();
    }
}

std::span<const std::shared_ptr<PreferencesRow>> PreferencesGroup::rows() const {
    return impl_->rows;
}

SizeRequest PreferencesGroup::measure(const Constraints& constraints) const {
    const float header = impl_->title.empty() ? 0.0F : 30.0F;
    const float spacing = theme_number("row-spacing", 6.0F);
    SizeRequest result{0.0F, header, 0.0F, header};
    for (const auto& row : impl_->rows) {
        const auto request = row->measure_for_diagnostics(constraints);
        result.minimum_width = std::max(result.minimum_width, request.minimum_width);
        result.natural_width = std::max(result.natural_width, request.natural_width);
        result.minimum_height += request.minimum_height + spacing;
        result.natural_height += request.natural_height + spacing;
    }
    return result;
}

void PreferencesGroup::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const float spacing = theme_number("row-spacing", 6.0F);
    float y = allocation.y + (impl_->title.empty() ? 0.0F : 30.0F);
    for (const auto& row : impl_->rows) {
        const float height =
            row->measure({0.0F, 0.0F, allocation.width, allocation.height}).natural_height;
        row->allocate({allocation.x, y, allocation.width, height});
        y += height + spacing;
    }
}

void PreferencesGroup::snapshot(SnapshotContext& ctx) const {
    if (!impl_->title.empty()) {
        const auto font =
            preference_font(theme_number("heading-font-size", 14.0F), FontWeight::Bold);
        ctx.add_text(
            {allocation().x, allocation().y}, impl_->title, theme_color("text-color"), font);
    }
    Widget::snapshot(ctx);
}

struct PreferencesPage::Impl {
    std::string title;
    std::string description;
    std::vector<std::shared_ptr<PreferencesGroup>> groups;
    Rect content_bounds{};
};

std::shared_ptr<PreferencesPage> PreferencesPage::create(std::string title,
                                                         std::string description) {
    return std::shared_ptr<PreferencesPage>(
        new PreferencesPage(std::move(title), std::move(description)));
}

PreferencesPage::PreferencesPage(std::string title, std::string description)
    : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    impl_->description = std::move(description);
    add_style_class("preferences-page");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name(impl_->title.empty() ? "Preferences" : impl_->title);
    accessible.set_description(impl_->description);
}

PreferencesPage::~PreferencesPage() = default;

std::string_view PreferencesPage::title() const {
    return impl_->title;
}

void PreferencesPage::set_title(std::string title) {
    if (impl_->title != title) {
        impl_->title = std::move(title);
        ensure_accessible().set_name(impl_->title.empty() ? "Preferences" : impl_->title);
        queue_layout();
    }
}

std::string_view PreferencesPage::description() const {
    return impl_->description;
}

void PreferencesPage::set_description(std::string description) {
    if (impl_->description != description) {
        impl_->description = std::move(description);
        ensure_accessible().set_description(impl_->description);
        queue_layout();
    }
}

void PreferencesPage::add(std::shared_ptr<PreferencesGroup> group) {
    if (group == nullptr ||
        std::find(impl_->groups.begin(), impl_->groups.end(), group) != impl_->groups.end()) {
        return;
    }
    impl_->groups.push_back(group);
    append_child(std::move(group));
    queue_layout();
}

void PreferencesPage::remove(PreferencesGroup& group) {
    const auto iterator = std::find_if(impl_->groups.begin(),
                                       impl_->groups.end(),
                                       [&](const auto& item) { return item.get() == &group; });
    if (iterator != impl_->groups.end()) {
        remove_child(**iterator);
        impl_->groups.erase(iterator);
        queue_layout();
    }
}

std::span<const std::shared_ptr<PreferencesGroup>> PreferencesPage::groups() const {
    return impl_->groups;
}

SizeRequest PreferencesPage::measure(const Constraints& constraints) const {
    const float padding = theme_number("page-padding", 24.0F);
    const float spacing = theme_number("group-spacing", 24.0F);
    const float header = impl_->title.empty() && impl_->description.empty() ? 0.0F : 64.0F;
    SizeRequest result{0.0F, header + (padding * 2.0F), 0.0F, header + (padding * 2.0F)};
    for (const auto& group : impl_->groups) {
        const auto request = group->measure_for_diagnostics(constraints);
        result.minimum_width = std::max(result.minimum_width, request.minimum_width);
        result.natural_width = std::max(result.natural_width, request.natural_width);
        result.minimum_height += request.minimum_height + spacing;
        result.natural_height += request.natural_height + spacing;
    }
    const float maximum_width = theme_number("maximum-width", 720.0F);
    result.minimum_width = std::min(result.minimum_width + (padding * 2.0F), maximum_width);
    result.natural_width = std::max(
        result.minimum_width, std::min(result.natural_width + (padding * 2.0F), maximum_width));
    return result;
}

void PreferencesPage::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const float padding = theme_number("page-padding", 24.0F);
    const float spacing = theme_number("group-spacing", 24.0F);
    const float width = std::min(std::max(0.0F, allocation.width - (padding * 2.0F)),
                                 theme_number("maximum-width", 720.0F));
    const float x = allocation.x + ((allocation.width - width) * 0.5F);
    float y = allocation.y + padding +
              (impl_->title.empty() && impl_->description.empty() ? 0.0F : 64.0F);
    impl_->content_bounds = {
        x, allocation.y + padding, width, allocation.height - (padding * 2.0F)};
    for (const auto& group : impl_->groups) {
        const float height = group->measure({0.0F, 0.0F, width, allocation.height}).natural_height;
        group->allocate({x, y, width, height});
        y += height + spacing;
    }
}

void PreferencesPage::snapshot(SnapshotContext& ctx) const {
    float y = impl_->content_bounds.y;
    if (!impl_->title.empty()) {
        const auto font =
            preference_font(theme_number("page-title-font-size", 18.0F), FontWeight::Bold);
        ctx.add_text({impl_->content_bounds.x, y}, impl_->title, theme_color("text-color"), font);
        y += measure_text(impl_->title, font).height + 6.0F;
    }
    if (!impl_->description.empty()) {
        const auto font = preference_font(theme_number("description-font-size", 13.0F));
        add_text_elided(ctx,
                        {impl_->content_bounds.x, y},
                        impl_->description,
                        impl_->content_bounds.width,
                        theme_color("description-color",
                                    theme_color("text-secondary", Color::from_rgb(96, 103, 114))),
                        font);
    }
    Widget::snapshot(ctx);
}

} // namespace nk
