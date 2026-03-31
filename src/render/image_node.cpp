#include <nk/render/image_node.h>

namespace nk {

ImageNode::ImageNode(Rect dest, uint32_t const* pixel_data, int src_width,
                     int src_height, ScaleMode scale)
    : RenderNode(RenderNodeKind::Image),
      pixel_data_(pixel_data),
      src_width_(src_width),
      src_height_(src_height),
      scale_mode_(scale) {
    set_bounds(dest);
}

uint32_t const* ImageNode::pixel_data() const { return pixel_data_; }
int ImageNode::src_width() const { return src_width_; }
int ImageNode::src_height() const { return src_height_; }
ScaleMode ImageNode::scale_mode() const { return scale_mode_; }

} // namespace nk
