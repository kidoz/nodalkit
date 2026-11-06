#include <nk/widgets/headerbar.h>
#include <nk/widgets/navigation_stack.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nk {

namespace {

void require_page(const std::shared_ptr<Widget>& page) {
    if (page == nullptr) {
        throw std::invalid_argument("NavigationStack page must not be null");
    }
}

} // namespace

struct NavigationStack::Impl {
    struct Page {
        std::string title;
        std::shared_ptr<Widget> content;
    };

    std::vector<Page> pages;
    std::weak_ptr<Widget> headerbar;
    ScopedConnection back_connection;
    Signal<std::size_t> depth_changed;
};

std::shared_ptr<NavigationStack> NavigationStack::create() {
    return std::shared_ptr<NavigationStack>(new NavigationStack());
}

NavigationStack::NavigationStack() : impl_(std::make_unique<Impl>()) {
    add_style_class("navigation-stack");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name("Navigation");
}

NavigationStack::~NavigationStack() = default;

void NavigationStack::set_root(std::shared_ptr<Widget> page, std::string title) {
    require_page(page);
    discard_to(0);
    impl_->pages.push_back({std::move(title), std::move(page)});
    append_child(impl_->pages.back().content);
    reveal_top();
}

void NavigationStack::push(std::shared_ptr<Widget> page, std::string title) {
    require_page(page);
    if (!impl_->pages.empty()) {
        impl_->pages.back().content->set_visible(false);
    }
    impl_->pages.push_back({std::move(title), std::move(page)});
    append_child(impl_->pages.back().content);
    reveal_top();
}

void NavigationStack::pop() {
    if (impl_->pages.size() <= 1) {
        return;
    }
    pop_to(impl_->pages.size() - 1);
}

void NavigationStack::pop_to(std::size_t depth) {
    if (depth >= impl_->pages.size()) {
        return;
    }
    discard_to(depth);
    reveal_top();
}

void NavigationStack::discard_to(std::size_t depth) {
    while (impl_->pages.size() > depth) {
        auto page = std::move(impl_->pages.back());
        impl_->pages.pop_back();
        page.content->set_visible(false);
        remove_child(*page.content);
    }
}

std::size_t NavigationStack::depth() const {
    return impl_->pages.size();
}

std::string_view NavigationStack::title() const {
    return impl_->pages.empty() ? std::string_view{} : std::string_view{impl_->pages.back().title};
}

Widget* NavigationStack::current() const {
    return impl_->pages.empty() ? nullptr : impl_->pages.back().content.get();
}

void NavigationStack::bind_headerbar(Headerbar& headerbar) {
    impl_->headerbar = headerbar.weak_from_this();
    impl_->back_connection =
        ScopedConnection{headerbar.on_back_requested().connect([weak = weak_from_this()] {
            auto self = std::static_pointer_cast<NavigationStack>(weak.lock());
            if (self != nullptr) {
                self->pop();
            }
        })};
    sync_headerbar();
}

void NavigationStack::unbind_headerbar() {
    impl_->back_connection.disconnect();
    if (auto widget = impl_->headerbar.lock()) {
        if (auto* headerbar = dynamic_cast<Headerbar*>(widget.get())) {
            headerbar->set_show_back_button(false);
        }
    }
    impl_->headerbar.reset();
}

Signal<std::size_t>& NavigationStack::on_depth_changed() {
    return impl_->depth_changed;
}

void NavigationStack::reveal_top() {
    for (std::size_t index = 0; index < impl_->pages.size(); ++index) {
        auto& page = impl_->pages[index];
        const bool top = index + 1 == impl_->pages.size();
        page.content->set_visible(top);
        if (top) {
            page.content->allocate(allocation());
        }
    }
    sync_headerbar();
    queue_layout();
    impl_->depth_changed.emit(impl_->pages.size());
}

void NavigationStack::sync_headerbar() {
    auto widget = impl_->headerbar.lock();
    if (widget == nullptr) {
        return;
    }
    auto* headerbar = dynamic_cast<Headerbar*>(widget.get());
    if (headerbar == nullptr) {
        return;
    }
    headerbar->set_show_back_button(impl_->pages.size() > 1);
    if (!impl_->pages.empty()) {
        headerbar->set_title(impl_->pages.back().title);
    }
}

SizeRequest NavigationStack::measure(const Constraints& constraints) const {
    if (impl_->pages.empty()) {
        return {};
    }
    return impl_->pages.back().content->measure_for_diagnostics(constraints);
}

void NavigationStack::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (!impl_->pages.empty()) {
        impl_->pages.back().content->allocate(allocation);
    }
}

} // namespace nk
