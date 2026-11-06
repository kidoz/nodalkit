#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/busy_overlay.h>
#include <nk/widgets/label.h>
#include <nk/widgets/spinner.h>
#include <utility>

namespace nk {

namespace {

class BusyScrim : public Widget {
public:
    static std::shared_ptr<BusyScrim> create() {
        return std::shared_ptr<BusyScrim>(new BusyScrim());
    }

    bool handle_mouse_event(const MouseEvent& /*event*/) override { return true; }

    bool handle_key_event(const KeyEvent& event) override {
        return event.type == KeyEvent::Type::Press || event.type == KeyEvent::Type::Release;
    }

    void set_contents(std::shared_ptr<Widget> spinner, std::shared_ptr<Widget> message) {
        append_child(std::move(spinner));
        append_child(std::move(message));
    }

protected:
    BusyScrim() {
        set_focusable(true);
        add_style_class("busy-scrim");
        auto& accessible = ensure_accessible();
        accessible.set_role(AccessibleRole::Status);
        accessible.set_name("Busy");
    }

    void snapshot(SnapshotContext& ctx) const override {
        ctx.add_color_rect(allocation(),
                           theme_color("scrim-color", Color{0.10F, 0.12F, 0.16F, 0.45F}));
        Widget::snapshot(ctx);
    }
};

} // namespace

struct BusyOverlay::Impl {
    std::shared_ptr<Widget> page;
    std::shared_ptr<BusyScrim> scrim;
    std::shared_ptr<Spinner> spinner;
    std::shared_ptr<Label> message_label;
    std::string message;
    bool busy = false;
    bool page_was_sensitive = true;
    std::weak_ptr<Widget> focus_restore;
};

std::shared_ptr<BusyOverlay> BusyOverlay::create() {
    return std::shared_ptr<BusyOverlay>(new BusyOverlay());
}

BusyOverlay::BusyOverlay() : impl_(std::make_unique<Impl>()) {
    add_style_class("busy-overlay");
    impl_->scrim = BusyScrim::create();
    impl_->spinner = Spinner::create();
    impl_->spinner->set_diameter(32.0F);
    impl_->spinner->set_spinning(false);
    impl_->message_label = Label::create("");
    impl_->scrim->set_contents(impl_->spinner, impl_->message_label);
    impl_->message_label->set_visible(false);
    impl_->scrim->set_visible(false);
    append_child(impl_->scrim);
}

BusyOverlay::~BusyOverlay() = default;

void BusyOverlay::set_child(std::shared_ptr<Widget> page) {
    if (impl_->page == page) {
        return;
    }
    if (impl_->page != nullptr) {
        if (impl_->busy && impl_->page_was_sensitive) {
            impl_->page->set_sensitive(true);
        }
        remove_child(*impl_->page);
    }
    impl_->page = std::move(page);
    impl_->page_was_sensitive = impl_->page == nullptr || impl_->page->is_sensitive();
    if (impl_->page != nullptr) {
        insert_child(0, impl_->page);
        if (impl_->busy) {
            impl_->page->set_sensitive(false);
        }
    }
    queue_layout();
}

Widget* BusyOverlay::child() const {
    return impl_->page.get();
}

bool BusyOverlay::is_busy() const {
    return impl_->busy;
}

void BusyOverlay::set_busy(bool busy) {
    if (impl_->busy == busy) {
        return;
    }
    impl_->busy = busy;
    if (busy) {
        if (impl_->page != nullptr) {
            impl_->page_was_sensitive = impl_->page->is_sensitive();
            if (auto* focused = host_focused_widget();
                focused != nullptr && contains_page(focused)) {
                impl_->focus_restore = focused->weak_from_this();
            }
            impl_->page->set_sensitive(false);
        }
        impl_->scrim->set_visible(true);
        impl_->scrim->grab_focus();
        impl_->spinner->set_spinning(true);
    } else {
        impl_->spinner->set_spinning(false);
        impl_->scrim->set_visible(false);
        if (impl_->page != nullptr) {
            impl_->page->set_sensitive(impl_->page_was_sensitive);
        }
        if (auto previous = impl_->focus_restore.lock()) {
            previous->grab_focus();
        }
        impl_->focus_restore.reset();
    }
    place_scrim_contents();
    queue_redraw();
}

std::string_view BusyOverlay::message() const {
    return impl_->message;
}

void BusyOverlay::set_message(std::string message) {
    if (impl_->message == message) {
        return;
    }
    impl_->message = std::move(message);
    impl_->message_label->set_text(impl_->message);
    impl_->message_label->set_visible(impl_->busy && !impl_->message.empty());
    impl_->scrim->ensure_accessible().set_name(impl_->message.empty() ? "Busy" : impl_->message);
    place_scrim_contents();
    queue_redraw();
}

bool BusyOverlay::contains_page(const Widget* widget) const {
    if (impl_->page == nullptr || widget == nullptr) {
        return false;
    }
    for (const auto* current = widget; current != nullptr; current = current->parent()) {
        if (current == impl_->page.get()) {
            return true;
        }
    }
    return false;
}

void BusyOverlay::place_scrim_contents() {
    const auto bounds = impl_->scrim->allocation();
    const float diameter = 32.0F;
    const bool show_message = impl_->busy && !impl_->message.empty();
    impl_->message_label->set_visible(show_message);
    const auto message_size =
        show_message ? impl_->message_label->measure(Constraints::unbounded()) : SizeRequest{};
    const float gap = show_message ? 8.0F : 0.0F;
    const float block_height = diameter + gap + message_size.natural_height;
    const float top = bounds.y + std::max(0.0F, (bounds.height - block_height) * 0.5F);
    impl_->spinner->allocate(
        {bounds.x + ((bounds.width - diameter) * 0.5F), top, diameter, diameter});
    if (show_message) {
        const float message_width = std::min(message_size.natural_width, bounds.width);
        impl_->message_label->allocate({bounds.x + ((bounds.width - message_width) * 0.5F),
                                        top + diameter + gap,
                                        message_width,
                                        message_size.natural_height});
    }
}

SizeRequest BusyOverlay::measure(const Constraints& constraints) const {
    if (impl_->page == nullptr) {
        return {200.0F, 120.0F, 200.0F, 120.0F};
    }
    return impl_->page->measure_for_diagnostics(constraints);
}

void BusyOverlay::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (impl_->page != nullptr) {
        impl_->page->allocate(allocation);
    }
    impl_->scrim->allocate(allocation);
    place_scrim_contents();
}

} // namespace nk
