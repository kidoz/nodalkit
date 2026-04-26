#include <nk/text/shaped_text.h>

namespace nk {

struct ShapedText::Impl {
    Size text_size{};
    float baseline = 0;
    std::vector<uint8_t> bitmap_data;
    int bitmap_width = 0;
    int bitmap_height = 0;
};

ShapedText::ShapedText() : impl_(std::make_unique<Impl>()) {}

ShapedText::~ShapedText() = default;
ShapedText::ShapedText(ShapedText&&) noexcept = default;
ShapedText& ShapedText::operator=(ShapedText&&) noexcept = default;

Size ShapedText::text_size() const {
    return impl_->text_size;
}

void ShapedText::set_text_size(Size size) {
    impl_->text_size = size;
}

float ShapedText::baseline() const {
    return impl_->baseline;
}

void ShapedText::set_baseline(float baseline) {
    impl_->baseline = baseline;
}

const uint8_t* ShapedText::bitmap_data() const {
    return impl_->bitmap_data.empty() ? nullptr : impl_->bitmap_data.data();
}

int ShapedText::bitmap_width() const {
    return impl_->bitmap_width;
}

int ShapedText::bitmap_height() const {
    return impl_->bitmap_height;
}

void ShapedText::set_bitmap(std::vector<uint8_t> data, int width, int height) {
    impl_->bitmap_data = std::move(data);
    impl_->bitmap_width = width;
    impl_->bitmap_height = height;
}

} // namespace nk
