#include <algorithm>
#include <nk/layout/box_layout.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/about_dialog.h>
#include <nk/widgets/label.h>
#include <nk/widgets/preferences.h>

namespace nk {

namespace {

/// A plain vertical container. The About window stacks a handful of labels and
/// one boxed list, so it needs a container but no behavior of its own.
class VerticalBox : public Widget {
public:
    static std::shared_ptr<VerticalBox> create(float spacing) {
        auto box = std::shared_ptr<VerticalBox>(new VerticalBox());
        auto layout = std::make_unique<BoxLayout>(Orientation::Vertical);
        layout->set_spacing(spacing);
        box->set_layout_manager(std::move(layout));
        return box;
    }

    void append(std::shared_ptr<Widget> child) { append_child(std::move(child)); }

private:
    VerticalBox() = default;
};

constexpr float kLogoMaxExtent = 128.0F;

/// Gives the logo the full content width and centers the mark inside it.
class CenteredLogo : public Widget {
public:
    static std::shared_ptr<CenteredLogo> create(std::shared_ptr<Widget> logo) {
        auto frame = std::shared_ptr<CenteredLogo>(new CenteredLogo(std::move(logo)));
        frame->set_horizontal_size_policy(SizePolicy::Expanding);
        frame->append_child(frame->logo_);
        return frame;
    }

    [[nodiscard]] SizeRequest measure(const Constraints& /*constraints*/) const override {
        const auto request = logo_->measure(Constraints::unbounded());
        const float width = std::clamp(request.natural_width, 1.0F, kLogoMaxExtent);
        const float height = std::clamp(request.natural_height, 1.0F, kLogoMaxExtent);
        return {width, height, width, height};
    }

    void allocate(const Rect& allocation) override {
        Widget::allocate(allocation);
        const auto request = logo_->measure(Constraints::unbounded());
        const float width =
            std::min(request.natural_width, std::min(allocation.width, kLogoMaxExtent));
        const float height =
            std::min(request.natural_height, std::min(allocation.height, kLogoMaxExtent));
        logo_->allocate({allocation.x + ((allocation.width - width) * 0.5F),
                         allocation.y + ((allocation.height - height) * 0.5F),
                         width,
                         height});
    }

private:
    explicit CenteredLogo(std::shared_ptr<Widget> logo) : logo_(std::move(logo)) {}

    std::shared_ptr<Widget> logo_;
};

std::shared_ptr<Label> centered_label(std::string text, std::string style_class) {
    auto label = Label::create(std::move(text));
    label->set_h_align(HAlign::Center);
    label->set_wrapping(true);
    if (!style_class.empty()) {
        label->add_style_class(std::move(style_class));
    }
    return label;
}

} // namespace

struct AboutDialog::Impl {
    AboutInfo info;
    std::shared_ptr<Dialog> dialog;
    Signal<std::string_view> link_activated;
    std::vector<ScopedConnection> row_connections;
};

std::shared_ptr<AboutDialog> AboutDialog::create(AboutInfo info) {
    return std::shared_ptr<AboutDialog>(new AboutDialog(std::move(info)));
}

AboutDialog::AboutDialog(AboutInfo info) : impl_(std::make_unique<Impl>()) {
    impl_->info = std::move(info);
    const auto& about = impl_->info;

    impl_->dialog = Dialog::create("About " + about.application_name);
    impl_->dialog->set_minimum_panel_width(400.0F);

    auto content = VerticalBox::create(8.0F);
    content->add_style_class("about-dialog");

    // Identity first: optional logo, then name, version, and what the app is for.
    if (about.logo != nullptr) {
        content->append(CenteredLogo::create(about.logo));
    }
    content->append(centered_label(about.application_name, "about-name"));
    if (!about.version.empty()) {
        content->append(centered_label(about.version, "about-version"));
    }
    if (!about.comments.empty()) {
        content->append(centered_label(about.comments, "about-comments"));
    }
    if (!about.developer_name.empty()) {
        content->append(centered_label(about.developer_name, "about-developer"));
    }

    // Links as a boxed list of activatable rows, the GNOME shape for this.
    auto links = PreferencesGroup::create();
    const auto add_link_row = [&](std::string title, const std::string& url) {
        if (url.empty()) {
            return;
        }
        auto row = PreferencesRow::create(std::move(title), url);
        row->set_activatable(true);
        impl_->row_connections.emplace_back(
            row->on_activated().connect([this, url] { impl_->link_activated.emit(url); }));
        links->add(row);
    };
    add_link_row("Website", about.website);
    add_link_row("Report an Issue", about.issue_url);
    if (!links->rows().empty()) {
        content->append(links);
    }

    if (!about.credits.empty()) {
        auto credits = PreferencesGroup::create("Credits");
        for (const auto& entry : about.credits) {
            credits->add(PreferencesRow::create(entry));
        }
        content->append(credits);
    }

    if (!about.copyright.empty()) {
        content->append(centered_label(about.copyright, "about-legal"));
    }
    if (!about.license.empty()) {
        content->append(centered_label(about.license, "about-legal"));
    }

    impl_->dialog->set_content(content);
    impl_->dialog->add_button("Close", DialogResponse::Close);
}

AboutDialog::~AboutDialog() = default;

void AboutDialog::present(Window& parent) {
    impl_->dialog->present(parent);
}

Signal<std::string_view>& AboutDialog::on_link_activated() {
    return impl_->link_activated;
}

Dialog& AboutDialog::dialog() {
    return *impl_->dialog;
}

} // namespace nk
