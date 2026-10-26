#include <nk/render/image_node.h>
#include <stdexcept>

namespace nk {

ImageNode::ImageNode(
    Rect dest, const uint32_t* pixel_data, int src_width, int src_height, ScaleMode scale)
    : RenderNode(RenderNodeKind::Image)
    , pixel_data_(pixel_data)
    , src_width_(src_width)
    , src_height_(src_height)
    , scale_mode_(scale) {
    set_bounds(dest);
}

ImageNode::ImageNode(Rect dest,
                     std::shared_ptr<const std::vector<uint32_t>> pixels,
                     int src_width,
                     int src_height,
                     ScaleMode scale)
    : ImageNode(dest, pixels ? pixels->data() : nullptr, src_width, src_height, scale) {
    if (src_width < 0 || src_height < 0 || !pixels ||
        (src_height > 0 && static_cast<std::size_t>(src_width) >
                               pixels->size() / static_cast<std::size_t>(src_height))) {
        throw std::invalid_argument("Image dimensions exceed the pixel buffer");
    }
    pixels_ = std::move(pixels);
}

const uint32_t* ImageNode::pixel_data() const {
    return pixel_data_;
}

int ImageNode::src_width() const {
    return src_width_;
}

int ImageNode::src_height() const {
    return src_height_;
}

ScaleMode ImageNode::scale_mode() const {
    return scale_mode_;
}

} // namespace nk
