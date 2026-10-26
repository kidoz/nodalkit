#include <cmath>
#include <nk/layout/box_layout.h>
#include <nk/widgets/box.h>
#include <stdexcept>
#include <utility>

namespace nk {

std::shared_ptr<Box> Box::create(Orientation orientation, float spacing) {
    return std::shared_ptr<Box>(new Box(orientation, spacing));
}

Box::Box(Orientation orientation, float spacing) {
    set_layout_manager(std::make_unique<BoxLayout>(orientation));
    set_spacing(spacing);
}

Box::~Box() = default;

BoxLayout& Box::box_layout() const {
    auto* layout = dynamic_cast<BoxLayout*>(layout_manager());
    if (layout == nullptr) {
        throw std::logic_error("Box requires a BoxLayout");
    }
    return *layout;
}

Orientation Box::orientation() const {
    return box_layout().orientation();
}

void Box::set_orientation(Orientation orientation) {
    auto& layout = box_layout();
    if (layout.orientation() != orientation) {
        layout.set_orientation(orientation);
        queue_layout();
    }
}

float Box::spacing() const {
    return box_layout().spacing();
}

void Box::set_spacing(float spacing) {
    if (!std::isfinite(spacing) || spacing < 0.0F) {
        throw std::invalid_argument("Box spacing must be finite and nonnegative");
    }
    auto& layout = box_layout();
    if (layout.spacing() != spacing) {
        layout.set_spacing(spacing);
        queue_layout();
    }
}

bool Box::homogeneous() const {
    return box_layout().homogeneous();
}

void Box::set_homogeneous(bool homogeneous) {
    auto& layout = box_layout();
    if (layout.homogeneous() != homogeneous) {
        layout.set_homogeneous(homogeneous);
        queue_layout();
    }
}

void Box::append(std::shared_ptr<Widget> child) {
    append_child(std::move(child));
}

void Box::prepend(std::shared_ptr<Widget> child) {
    insert_child(0, std::move(child));
}

void Box::insert(std::size_t index, std::shared_ptr<Widget> child) {
    insert_child(index, std::move(child));
}

void Box::remove(Widget& child) {
    remove_child(child);
}

void Box::clear() {
    clear_children();
}

} // namespace nk
