#include <algorithm>
#include <nk/layout/breakpoint.h>

namespace nk {

bool BreakpointCondition::matches(Size size) const {
    return (!min_width.has_value() || size.width >= *min_width) &&
           (!max_width.has_value() || size.width <= *max_width) &&
           (!min_height.has_value() || size.height >= *min_height) &&
           (!max_height.has_value() || size.height <= *max_height);
}

std::shared_ptr<Breakpoint> Breakpoint::create(BreakpointCondition condition) {
    return std::shared_ptr<Breakpoint>(new Breakpoint(std::move(condition)));
}

Breakpoint::Breakpoint(BreakpointCondition condition) : condition_(std::move(condition)) {}

const BreakpointCondition& Breakpoint::condition() const {
    return condition_;
}

void Breakpoint::set_condition(BreakpointCondition condition) {
    condition_ = std::move(condition);
    if (last_size_.has_value()) {
        set_active(condition_.matches(*last_size_));
    }
}

bool Breakpoint::is_active() const {
    return active_;
}

Signal<bool>& Breakpoint::on_active_changed() {
    return active_changed_;
}

void Breakpoint::update(Size size) {
    last_size_ = size;
    set_active(condition_.matches(size));
}

void Breakpoint::set_active(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    active_changed_.emit(active_);
}

struct BreakpointBin::Impl {
    std::shared_ptr<Widget> child;
    std::vector<std::shared_ptr<Breakpoint>> breakpoints;
};

std::shared_ptr<BreakpointBin> BreakpointBin::create() {
    return std::shared_ptr<BreakpointBin>(new BreakpointBin());
}

BreakpointBin::BreakpointBin() : impl_(std::make_unique<Impl>()) {
    add_style_class("breakpoint-bin");
}

BreakpointBin::~BreakpointBin() = default;

void BreakpointBin::set_child(std::shared_ptr<Widget> child) {
    if (impl_->child == child) {
        return;
    }
    if (impl_->child != nullptr) {
        remove_child(*impl_->child);
    }
    impl_->child = std::move(child);
    if (impl_->child != nullptr) {
        append_child(impl_->child);
    }
    queue_layout();
}

Widget* BreakpointBin::child() const {
    return impl_->child.get();
}

void BreakpointBin::add_breakpoint(std::shared_ptr<Breakpoint> breakpoint) {
    if (breakpoint == nullptr ||
        std::find(impl_->breakpoints.begin(), impl_->breakpoints.end(), breakpoint) !=
            impl_->breakpoints.end()) {
        return;
    }
    impl_->breakpoints.push_back(std::move(breakpoint));
    queue_layout();
}

void BreakpointBin::remove_breakpoint(Breakpoint& breakpoint) {
    auto iterator =
        std::find_if(impl_->breakpoints.begin(),
                     impl_->breakpoints.end(),
                     [&](const auto& candidate) { return candidate.get() == &breakpoint; });
    if (iterator == impl_->breakpoints.end()) {
        return;
    }
    auto removed = *iterator;
    impl_->breakpoints.erase(iterator);
    removed->set_active(false);
    queue_layout();
}

void BreakpointBin::clear_breakpoints() {
    auto breakpoints = std::move(impl_->breakpoints);
    impl_->breakpoints.clear();
    for (const auto& breakpoint : breakpoints) {
        breakpoint->set_active(false);
    }
    queue_layout();
}

std::span<const std::shared_ptr<Breakpoint>> BreakpointBin::breakpoints() const {
    return impl_->breakpoints;
}

bool BreakpointBin::has_height_for_width() const {
    return impl_->child != nullptr && impl_->child->has_height_for_width();
}

float BreakpointBin::height_for_width(float width) const {
    if (impl_->child == nullptr) {
        return 0.0F;
    }
    return impl_->child->has_height_for_width()
               ? impl_->child->height_for_width(width)
               : impl_->child->measure(Constraints::unbounded()).natural_height;
}

SizeRequest BreakpointBin::measure(const Constraints& constraints) const {
    return impl_->child != nullptr ? impl_->child->measure_for_diagnostics(constraints)
                                   : SizeRequest{};
}

void BreakpointBin::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const auto breakpoints = impl_->breakpoints;
    for (const auto& breakpoint : breakpoints) {
        breakpoint->update(allocation.size());
    }
    if (impl_->child != nullptr) {
        impl_->child->allocate(allocation);
    }
}

} // namespace nk
