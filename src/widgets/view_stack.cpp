#include <algorithm>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/view_stack.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nk {

namespace {

void require_page(const std::shared_ptr<Widget>& page) {
    if (page == nullptr) {
        throw std::invalid_argument("ViewStack page must not be null");
    }
}

} // namespace

struct ViewStack::Impl {
    struct Page {
        std::string title;
        std::shared_ptr<Widget> content;
    };

    std::vector<Page> pages;
    int selected_index = -1;
    std::weak_ptr<Widget> switcher;
    ScopedConnection selection_connection;
    bool updating = false;
    Signal<int> selection_changed;
};

std::shared_ptr<ViewStack> ViewStack::create() {
    return std::shared_ptr<ViewStack>(new ViewStack());
}

ViewStack::ViewStack() : impl_(std::make_unique<Impl>()) {
    add_style_class("view-stack");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name("Views");
}

ViewStack::~ViewStack() = default;

void ViewStack::append(std::shared_ptr<Widget> page, std::string title) {
    insert(impl_->pages.size(), std::move(page), std::move(title));
}

void ViewStack::insert(std::size_t index, std::shared_ptr<Widget> page, std::string title) {
    require_page(page);
    if (index > impl_->pages.size()) {
        throw std::out_of_range("ViewStack::insert: index out of range");
    }
    const int previous = impl_->selected_index;
    impl_->pages.insert(impl_->pages.begin() + static_cast<std::ptrdiff_t>(index),
                        {std::move(title), std::move(page)});
    insert_child(index, impl_->pages[index].content);
    if (impl_->selected_index < 0) {
        impl_->selected_index = static_cast<int>(index);
    } else if (static_cast<int>(index) <= impl_->selected_index) {
        ++impl_->selected_index;
    }
    reveal();
    sync_switcher();
    if (impl_->selected_index != previous) {
        impl_->selection_changed.emit(impl_->selected_index);
    }
}

void ViewStack::remove(Widget& page) {
    const auto found =
        std::find_if(impl_->pages.begin(), impl_->pages.end(), [&](const auto& entry) {
            return entry.content.get() == &page;
        });
    if (found == impl_->pages.end()) {
        return;
    }
    const auto index = static_cast<int>(found - impl_->pages.begin());
    const int previous = impl_->selected_index;
    found->content->set_visible(false);
    remove_child(*found->content);
    impl_->pages.erase(found);
    if (impl_->pages.empty()) {
        impl_->selected_index = -1;
    } else if (index < impl_->selected_index) {
        --impl_->selected_index;
    } else if (index == impl_->selected_index) {
        impl_->selected_index =
            std::min(impl_->selected_index, static_cast<int>(impl_->pages.size()) - 1);
    }
    reveal();
    sync_switcher();
    if (impl_->selected_index != previous) {
        impl_->selection_changed.emit(impl_->selected_index);
    }
}

void ViewStack::clear() {
    const int previous = impl_->selected_index;
    while (!impl_->pages.empty()) {
        auto page = std::move(impl_->pages.back());
        impl_->pages.pop_back();
        page.content->set_visible(false);
        remove_child(*page.content);
    }
    impl_->selected_index = -1;
    reveal();
    sync_switcher();
    if (previous != -1) {
        impl_->selection_changed.emit(-1);
    }
}

std::size_t ViewStack::page_count() const {
    return impl_->pages.size();
}

std::string_view ViewStack::title(std::size_t index) const {
    if (index >= impl_->pages.size()) {
        throw std::out_of_range("ViewStack::title: index out of range");
    }
    return impl_->pages[index].title;
}

Widget* ViewStack::page(std::size_t index) const {
    if (index >= impl_->pages.size()) {
        throw std::out_of_range("ViewStack::page: index out of range");
    }
    return impl_->pages[index].content.get();
}

int ViewStack::selected_index() const {
    return impl_->selected_index;
}

void ViewStack::set_selected_index(int index) {
    if (index == impl_->selected_index) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(impl_->pages.size())) {
        throw std::out_of_range("ViewStack::set_selected_index: index out of range");
    }
    impl_->selected_index = index;
    reveal();
    sync_switcher();
    impl_->selection_changed.emit(index);
}

Widget* ViewStack::current() const {
    if (impl_->selected_index < 0 ||
        impl_->selected_index >= static_cast<int>(impl_->pages.size())) {
        return nullptr;
    }
    return impl_->pages[static_cast<std::size_t>(impl_->selected_index)].content.get();
}

void ViewStack::bind_switcher(SegmentedControl& control) {
    impl_->switcher = control.weak_from_this();
    impl_->selection_connection = ScopedConnection{
        control.on_selection_changed().connect([weak = weak_from_this()](int index) {
            auto self = std::static_pointer_cast<ViewStack>(weak.lock());
            if (self == nullptr || self->impl_->updating) {
                return;
            }
            if (index == self->selected_index()) {
                return;
            }
            self->set_selected_index(index);
        })};
    sync_switcher();
}

void ViewStack::unbind_switcher() {
    impl_->selection_connection.disconnect();
    impl_->switcher.reset();
}

Signal<int>& ViewStack::on_selection_changed() {
    return impl_->selection_changed;
}

void ViewStack::reveal() {
    for (std::size_t index = 0; index < impl_->pages.size(); ++index) {
        auto& page = impl_->pages[index];
        const bool selected = static_cast<int>(index) == impl_->selected_index;
        page.content->set_visible(selected);
        if (selected) {
            page.content->allocate(allocation());
        }
    }
    queue_layout();
}

void ViewStack::sync_switcher() {
    auto widget = impl_->switcher.lock();
    if (widget == nullptr) {
        return;
    }
    auto* control = dynamic_cast<SegmentedControl*>(widget.get());
    if (control == nullptr) {
        return;
    }
    impl_->updating = true;
    std::vector<std::string> titles;
    titles.reserve(impl_->pages.size());
    for (const auto& page : impl_->pages) {
        titles.push_back(page.title);
    }
    control->set_segments(std::move(titles));
    if (impl_->selected_index >= 0 && control->selected_index() != impl_->selected_index) {
        control->set_selected_index(impl_->selected_index);
    }
    impl_->updating = false;
}

SizeRequest ViewStack::measure(const Constraints& constraints) const {
    SizeRequest result{};
    for (const auto& page : impl_->pages) {
        const auto request = page.content->measure_for_diagnostics(constraints);
        result.minimum_width = std::max(result.minimum_width, request.minimum_width);
        result.minimum_height = std::max(result.minimum_height, request.minimum_height);
        result.natural_width = std::max(result.natural_width, request.natural_width);
        result.natural_height = std::max(result.natural_height, request.natural_height);
    }
    return result;
}

void ViewStack::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (auto* page = current()) {
        page->allocate(allocation);
    }
}

} // namespace nk
