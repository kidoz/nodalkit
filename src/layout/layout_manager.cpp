#include <nk/layout/layout_manager.h>

namespace nk {

LayoutManager::LayoutManager() = default;
LayoutManager::~LayoutManager() = default;

bool LayoutManager::has_height_for_width(const Widget& /*widget*/) const {
    return false;
}

float LayoutManager::height_for_width(const Widget& /*widget*/, float /*width*/) const {
    return 0.0F;
}

} // namespace nk
