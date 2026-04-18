#include <nk/layout/layout_manager.h>

namespace nk {

LayoutManager::LayoutManager() = default;
LayoutManager::~LayoutManager() = default;

bool LayoutManager::has_height_for_width(Widget const& /*widget*/) const {
    return false;
}

float LayoutManager::height_for_width(Widget const& /*widget*/, float /*width*/) const {
    return 0.0F;
}

} // namespace nk
