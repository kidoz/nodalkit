#pragma once

/// @file showcase_widgets.h
/// @brief Custom widget primitives used by the showcase example.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/layout/box_layout.h>
#include <nk/platform/events.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/image_view.h>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace showcase_detail {

struct WrappedTextLayout {
    std::vector<std::string> lines;
    float width = 0.0F;
    float height = 0.0F;
    float line_height = 0.0F;
};

template <typename MeasureTextFn>
inline WrappedTextLayout wrap_text_lines(std::string_view text,
                                         MeasureTextFn&& measure_text,
                                         float max_width,
                                         float line_spacing) {
    WrappedTextLayout layout;
    const auto line_box = measure_text("Ag");
    layout.line_height = std::max(1.0F, line_box.height);

    if (text.empty()) {
        layout.lines.emplace_back();
        layout.height = layout.line_height;
        return layout;
    }

    const auto full_size = measure_text(text);
    const bool bounded = std::isfinite(max_width) && max_width > 0.0F;
    if (!bounded || full_size.width <= max_width) {
        layout.lines.emplace_back(text);
        layout.width = full_size.width;
        layout.height = layout.line_height;
        return layout;
    }

    std::istringstream words{std::string(text)};
    std::string word;
    std::string current_line;

    auto flush_line = [&](std::string line) {
        const auto line_size = measure_text(line);
        layout.width = std::max(layout.width, line_size.width);
        layout.lines.push_back(std::move(line));
    };

    while (words >> word) {
        const auto candidate = current_line.empty() ? word : current_line + ' ' + word;
        const auto candidate_size = measure_text(candidate);
        if (current_line.empty() || candidate_size.width <= max_width) {
            current_line = candidate;
            continue;
        }

        flush_line(std::move(current_line));
        current_line = word;
    }

    if (!current_line.empty()) {
        flush_line(std::move(current_line));
    }

    if (layout.lines.empty()) {
        layout.lines.emplace_back(std::string(text));
        layout.width = full_size.width;
    }

    const auto line_count = static_cast<float>(layout.lines.size());
    layout.height =
        (layout.line_height * line_count) + (line_spacing * std::max(0.0F, line_count - 1.0F));
    return layout;
}

} // namespace showcase_detail

class Box : public nk::Widget {
public:
    static std::shared_ptr<Box> vertical(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
        layout->set_spacing(spacing);
        box->set_layout_manager(std::move(layout));
        return box;
    }

    static std::shared_ptr<Box> horizontal(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
        layout->set_spacing(spacing);
        box->set_layout_manager(std::move(layout));
        return box;
    }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

private:
    Box() = default;
};

class Spacer : public nk::Widget {
public:
    static std::shared_ptr<Spacer> create() {
        auto spacer = std::shared_ptr<Spacer>(new Spacer());
        spacer->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        spacer->set_vertical_size_policy(nk::SizePolicy::Preferred);
        spacer->set_horizontal_stretch(1);
        return spacer;
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }

protected:
    void snapshot(nk::SnapshotContext& /*ctx*/) const override {}

private:
    Spacer() = default;
};

class SectionTitle : public nk::Widget {
public:
    static std::shared_ptr<SectionTitle> create(std::string text) {
        return std::shared_ptr<SectionTitle>(new SectionTitle(std::move(text)));
    }

    void set_text(std::string text) {
        if (text_ != text) {
            text_ = std::move(text);
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float height = std::max(28.0F, cached_size_->height);
        return {cached_size_->width, height, cached_size_->width, height};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float text_y = a.y + std::max(0.0F, (a.height - cached_size_->height) * 0.5F);
        ctx.add_text({a.x, text_y},
                     text_,
                     theme_color("text-color", nk::Color{0.14F, 0.16F, 0.19F, 1.0F}),
                     font_descriptor());
    }

private:
    explicit SectionTitle(std::string text) : text_(std::move(text)) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 18.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    std::string text_;
    mutable std::optional<nk::Size> cached_size_;
};

class SecondaryText : public nk::Widget {
public:
    static std::shared_ptr<SecondaryText> create(std::string text) {
        return std::shared_ptr<SecondaryText>(new SecondaryText(std::move(text)));
    }

    void set_text(std::string text) {
        if (text_ != text) {
            text_ = std::move(text);
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        const auto layout = wrapped_layout(constraints.max_width);
        const float height = std::max(20.0F, layout.height);
        return {0.0F, height, layout.width, height};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const auto layout = wrapped_layout(a.width);
        float text_y = a.y;
        if (layout.lines.size() == 1) {
            text_y = a.y + std::max(0.0F, (a.height - layout.line_height) * 0.5F);
        }
        const auto color = theme_color("text-color", nk::Color{0.40F, 0.44F, 0.49F, 1.0F});
        for (const auto& line : layout.lines) {
            ctx.add_text({a.x, text_y}, line, color, font_descriptor());
            text_y += layout.line_height + 2.0F;
        }
    }

private:
    explicit SecondaryText(std::string text) : text_(std::move(text)) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 13.0F,
            .weight = nk::FontWeight::Regular,
        };
    }

    [[nodiscard]] showcase_detail::WrappedTextLayout wrapped_layout(float max_width) const {
        return showcase_detail::wrap_text_lines(
            text_,
            [&](std::string_view line) { return measure_text(line, font_descriptor()); },
            max_width,
            2.0F);
    }

    std::string text_;
};

class FieldLabel : public nk::Widget {
public:
    static std::shared_ptr<FieldLabel> create(std::string text) {
        return std::shared_ptr<FieldLabel>(new FieldLabel(std::move(text)));
    }

    void set_text(std::string text) {
        if (text_ != text) {
            text_ = std::move(text);
            cached_size_.reset();
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float height = std::max(18.0F, cached_size_->height);
        return {cached_size_->width, height, cached_size_->width, height};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float text_y = a.y + std::max(0.0F, (a.height - cached_size_->height) * 0.5F);
        ctx.add_text({a.x, text_y},
                     text_,
                     theme_color("text-color", nk::Color{0.52F, 0.56F, 0.61F, 1.0F}),
                     font_descriptor());
    }

private:
    explicit FieldLabel(std::string text) : text_(std::move(text)) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 12.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    std::string text_;
    mutable std::optional<nk::Size> cached_size_;
};

class ValueText : public nk::Widget {
public:
    static std::shared_ptr<ValueText> create(std::string text) {
        return std::shared_ptr<ValueText>(new ValueText(std::move(text)));
    }

    void set_text(std::string text) {
        if (text_ != text) {
            text_ = std::move(text);
            cached_size_.reset();
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float height = std::max(24.0F, cached_size_->height);
        return {cached_size_->width, height, cached_size_->width, height};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float text_y = a.y + std::max(0.0F, (a.height - cached_size_->height) * 0.5F);
        ctx.add_text({a.x, text_y},
                     text_,
                     theme_color("text-color", nk::Color{0.16F, 0.18F, 0.22F, 1.0F}),
                     font_descriptor());
    }

private:
    explicit ValueText(std::string text) : text_(std::move(text)) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 17.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    std::string text_;
    mutable std::optional<nk::Size> cached_size_;
};

class SurfacePanel : public nk::Widget {
public:
    static std::shared_ptr<SurfacePanel> page(std::shared_ptr<nk::Widget> content) {
        auto panel = std::shared_ptr<SurfacePanel>(new SurfacePanel(false));
        panel->add_style_class("page");
        panel->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        panel->set_vertical_size_policy(nk::SizePolicy::Expanding);
        panel->set_content(std::move(content));
        return panel;
    }

    static std::shared_ptr<SurfacePanel> card(std::shared_ptr<nk::Widget> content) {
        auto panel = std::shared_ptr<SurfacePanel>(new SurfacePanel(true));
        panel->add_style_class("card");
        panel->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        panel->set_content(std::move(content));
        return panel;
    }

    void set_content(std::shared_ptr<nk::Widget> content) {
        if (content_) {
            remove_child(*content_);
        }
        content_ = std::move(content);
        if (content_) {
            append_child(content_);
        }
        queue_layout();
    }

    void set_padding(float padding) { set_padding(padding, padding, padding, padding); }

    void set_padding(float top, float right, float bottom, float left) {
        padding_top_ = top;
        padding_right_ = right;
        padding_bottom_ = bottom;
        padding_left_ = left;
        queue_layout();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        if (!content_) {
            return {
                padding_left_ + padding_right_,
                padding_top_ + padding_bottom_,
                padding_left_ + padding_right_,
                padding_top_ + padding_bottom_,
            };
        }

        const auto req = content_->measure(constraints);
        return {
            req.minimum_width + padding_left_ + padding_right_,
            req.minimum_height + padding_top_ + padding_bottom_,
            req.natural_width + padding_left_ + padding_right_,
            req.natural_height + padding_top_ + padding_bottom_,
        };
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (!content_) {
            return;
        }

        const float inset = bordered_ ? 1.0F : 0.0F;
        content_->allocate({
            allocation.x + padding_left_ + inset,
            allocation.y + padding_top_ + inset,
            std::max(0.0F, allocation.width - padding_left_ - padding_right_ - (inset * 2.0F)),
            std::max(0.0F, allocation.height - padding_top_ - padding_bottom_ - (inset * 2.0F)),
        });
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const auto background = theme_color("background", nk::Color{1.0F, 1.0F, 1.0F, 1.0F});
        const float corner_radius = theme_number("corner-radius", 12.0F);

        if (bordered_) {
            ctx.add_rounded_rect({a.x, a.y + 1.0F, a.width, a.height},
                                 nk::Color{0.08F, 0.12F, 0.18F, 0.04F},
                                 corner_radius + 1.0F);
            ctx.add_rounded_rect(a, background, corner_radius);
            ctx.add_border(a,
                           theme_color("border-color", nk::Color{0.86F, 0.88F, 0.91F, 1.0F}),
                           1.0F,
                           corner_radius);
        } else {
            ctx.add_color_rect(a, background);
        }

        Widget::snapshot(ctx);
    }

private:
    explicit SurfacePanel(bool bordered) : bordered_(bordered) {}

    std::shared_ptr<nk::Widget> content_;
    bool bordered_ = false;
    float padding_top_ = 16.0F;
    float padding_right_ = 16.0F;
    float padding_bottom_ = 16.0F;
    float padding_left_ = 16.0F;
};

class StatusPill : public nk::Widget {
public:
    static std::shared_ptr<StatusPill> create(std::string text, bool emphasized = false) {
        return std::shared_ptr<StatusPill>(new StatusPill(std::move(text), emphasized));
    }

    void set_text(std::string text) {
        if (text_ != text) {
            text_ = std::move(text);
            cached_size_.reset();
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float width = cached_size_->width + 28.0F;
        return {width, 34.0F, width, 34.0F};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const auto background =
            emphasized_ ? theme_color("accent-color", nk::Color{0.15F, 0.48F, 0.47F, 1.0F})
                        : theme_color("surface-panel", nk::Color{0.94F, 0.96F, 0.98F, 1.0F});
        const auto border = emphasized_
                                ? theme_color("accent-color", nk::Color{0.15F, 0.48F, 0.47F, 1.0F})
                                : theme_color("border-color", nk::Color{0.84F, 0.87F, 0.91F, 1.0F});
        const auto text_color =
            emphasized_ ? nk::Color{1.0F, 1.0F, 1.0F, 1.0F}
                        : theme_color("text-color", nk::Color{0.28F, 0.31F, 0.36F, 1.0F});

        ctx.add_rounded_rect(a, background, a.height * 0.5F);
        ctx.add_border(a, border, 1.0F, a.height * 0.5F);

        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float text_x = a.x + std::max(0.0F, (a.width - cached_size_->width) * 0.5F);
        const float text_y = a.y + std::max(0.0F, (a.height - cached_size_->height) * 0.5F);
        ctx.add_text({text_x, text_y}, text_, text_color, font_descriptor());
    }

private:
    StatusPill(std::string text, bool emphasized)
        : text_(std::move(text)), emphasized_(emphasized) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 13.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    std::string text_;
    bool emphasized_ = false;
    mutable std::optional<nk::Size> cached_size_;
};

class HeaderActionButton : public nk::Widget {
public:
    static std::shared_ptr<HeaderActionButton> create(std::string glyph) {
        auto button = std::shared_ptr<HeaderActionButton>(new HeaderActionButton(std::move(glyph)));
        button->set_focusable(false);
        return button;
    }

    nk::Signal<>& on_clicked() { return clicked_; }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {28.0F, 28.0F, 28.0F, 28.0F};
    }

    bool handle_mouse_event(const nk::MouseEvent& event) override {
        if (event.button != 1) {
            return false;
        }

        switch (event.type) {
        case nk::MouseEvent::Type::Press:
            armed_ = allocation().contains({event.x, event.y});
            return armed_;
        case nk::MouseEvent::Type::Release: {
            const bool activate = armed_ && allocation().contains({event.x, event.y});
            armed_ = false;
            if (activate) {
                clicked_.emit();
            }
            return activate;
        }
        case nk::MouseEvent::Type::Move:
        case nk::MouseEvent::Type::Enter:
        case nk::MouseEvent::Type::Leave:
        case nk::MouseEvent::Type::Scroll:
            return false;
        }

        return false;
    }

    [[nodiscard]] nk::CursorShape cursor_shape() const override {
        return nk::CursorShape::PointingHand;
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const auto background =
            armed_ ? nk::Color{0.88F, 0.90F, 0.93F, 1.0F} : nk::Color{0.95F, 0.96F, 0.98F, 1.0F};
        const auto border = theme_color("border-color", nk::Color{0.84F, 0.87F, 0.91F, 1.0F});
        const auto text_color = theme_color("text-color", nk::Color{0.30F, 0.33F, 0.37F, 1.0F});
        const float radius = a.height * 0.5F;
        ctx.add_rounded_rect(a, background, radius);
        ctx.add_border(a, border, 1.0F, radius);

        const auto measured = measure_text(glyph_, glyph_font());
        const float text_x = a.x + std::max(0.0F, (a.width - measured.width) * 0.5F);
        const float text_y = a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
        ctx.add_text({text_x, text_y}, glyph_, text_color, glyph_font());
    }

private:
    explicit HeaderActionButton(std::string glyph) : glyph_(std::move(glyph)) {}

    static nk::FontDescriptor glyph_font() {
        return {
            .family = {},
            .size = 15.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    std::string glyph_;
    nk::Signal<> clicked_;
    bool armed_ = false;
};

class InsetStage : public nk::Widget {
public:
    static std::shared_ptr<InsetStage> create(std::shared_ptr<nk::Widget> content,
                                              float min_height,
                                              float natural_height,
                                              float padding = 10.0F) {
        auto stage =
            std::shared_ptr<InsetStage>(new InsetStage(min_height, natural_height, padding));
        stage->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        stage->set_content(std::move(content));
        return stage;
    }

    void set_content(std::shared_ptr<nk::Widget> content) {
        if (content_) {
            remove_child(*content_);
        }
        content_ = std::move(content);
        if (content_) {
            append_child(content_);
        }
        queue_layout();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        if (!content_) {
            return {0.0F, min_height_, 0.0F, natural_height_};
        }

        const auto req = content_->measure(constraints);
        return {
            req.minimum_width + (padding_ * 2.0F),
            std::max(min_height_, req.minimum_height + (padding_ * 2.0F)),
            req.natural_width + (padding_ * 2.0F),
            std::max(natural_height_, req.natural_height + (padding_ * 2.0F)),
        };
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (!content_) {
            return;
        }

        content_->allocate({
            allocation.x + padding_,
            allocation.y + padding_,
            std::max(0.0F, allocation.width - (padding_ * 2.0F)),
            std::max(0.0F, allocation.height - (padding_ * 2.0F)),
        });
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const float corner_radius = theme_number("corner-radius", 14.0F);
        ctx.add_rounded_rect(a,
                             theme_color("surface-panel", nk::Color{0.972F, 0.978F, 0.988F, 1.0F}),
                             corner_radius);
        ctx.add_border(a,
                       theme_color("border-color", nk::Color{0.88F, 0.90F, 0.93F, 1.0F}),
                       1.0F,
                       corner_radius);

        ctx.push_rounded_clip(a, corner_radius);
        Widget::snapshot(ctx);
        ctx.pop_container();
    }

private:
    InsetStage(float min_height, float natural_height, float padding)
        : min_height_(min_height), natural_height_(natural_height), padding_(padding) {}

    std::shared_ptr<nk::Widget> content_;
    float min_height_ = 0.0F;
    float natural_height_ = 0.0F;
    float padding_ = 0.0F;
};

class PreviewCanvas : public nk::Widget {
public:
    static std::shared_ptr<PreviewCanvas> create() {
        auto canvas = std::shared_ptr<PreviewCanvas>(new PreviewCanvas());
        canvas->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        canvas->set_vertical_size_policy(nk::SizePolicy::Expanding);
        canvas->set_vertical_stretch(1);
        return canvas;
    }

    void update_pixel_buffer(const uint32_t* data, int width, int height) {
        const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        pixels_.assign(data, data + count);
        src_width_ = width;
        src_height_ = height;
        queue_redraw();
    }

    void set_scale_mode(nk::ScaleMode mode) {
        if (scale_mode_ != mode) {
            scale_mode_ = mode;
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {280.0F, 280.0F, 420.0F, 320.0F};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const float sheet_radius = 22.0F;
        const auto sheet_bg = nk::Color{0.95F, 0.963F, 0.982F, 1.0F};
        const auto matte_bg = nk::Color{0.89F, 0.91F, 0.95F, 1.0F};
        const auto border = theme_color("border-color", nk::Color{0.87F, 0.89F, 0.92F, 1.0F});

        nk::Rect sheet = {
            a.x + 2.0F,
            a.y + 2.0F,
            std::max(0.0F, a.width - 4.0F),
            std::max(0.0F, a.height - 4.0F),
        };
        ctx.add_rounded_rect(sheet, sheet_bg, sheet_radius);
        ctx.add_border(sheet, border, 1.0F, sheet_radius);

        nk::Rect matte = {
            sheet.x + 10.0F,
            sheet.y + 10.0F,
            std::max(0.0F, sheet.width - 20.0F),
            std::max(0.0F, sheet.height - 20.0F),
        };

        if (pixels_.empty() || src_width_ <= 0 || src_height_ <= 0) {
            ctx.add_rounded_rect(matte, matte_bg, 16.0F);
            return;
        }

        const float scale = std::min(matte.width / static_cast<float>(src_width_),
                                     matte.height / static_cast<float>(src_height_));
        const float image_width = static_cast<float>(src_width_) * scale;
        const float image_height = static_cast<float>(src_height_) * scale;
        nk::Rect image_rect = {
            matte.x + std::max(0.0F, (matte.width - image_width) * 0.5F),
            matte.y + std::max(0.0F, (matte.height - image_height) * 0.5F),
            image_width,
            image_height,
        };

        ctx.add_rounded_rect(image_rect, matte_bg, 12.0F);
        ctx.push_rounded_clip(image_rect, 12.0F);
        ctx.add_image(image_rect, pixels_.data(), src_width_, src_height_, scale_mode_);
        ctx.pop_container();
    }

private:
    PreviewCanvas() = default;

    std::vector<uint32_t> pixels_;
    int src_width_ = 0;
    int src_height_ = 0;
    nk::ScaleMode scale_mode_ = nk::ScaleMode::NearestNeighbor;
};

class HeroBanner : public nk::Widget {
public:
    static std::shared_ptr<HeroBanner> create(std::string title,
                                              std::string subtitle,
                                              std::vector<std::shared_ptr<nk::Widget>> pills) {
        auto banner = std::shared_ptr<HeroBanner>(new HeroBanner());
        banner->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        banner->title_ = std::move(title);
        banner->subtitle_ = std::move(subtitle);
        banner->set_pills(std::move(pills));
        return banner;
    }

    void set_pills(std::vector<std::shared_ptr<nk::Widget>> pills) {
        for (const auto& pill : pills_) {
            if (pill) {
                remove_child(*pill);
            }
        }

        pills_ = std::move(pills);
        for (const auto& pill : pills_) {
            if (pill) {
                append_child(pill);
            }
        }
        queue_layout();
    }

    void set_height(float min_height, float natural_height) {
        min_height_ = min_height;
        natural_height_ = natural_height;
        queue_layout();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        if (!cached_title_size_) {
            cached_title_size_ = measure_text(title_, title_font());
        }
        const auto title_size = *cached_title_size_;

        float tray_width = 0.0F;
        float tray_height = 0.0F;
        for (std::size_t index = 0; index < pills_.size(); ++index) {
            if (!pills_[index]) {
                continue;
            }
            const auto req = pills_[index]->measure(constraints);
            tray_width += req.natural_width;
            tray_height = std::max(tray_height, req.natural_height);
            if (index + 1 < pills_.size()) {
                tray_width += 10.0F;
            }
        }

        float subtitle_width_limit = constraints.max_width;
        if (std::isfinite(subtitle_width_limit) && subtitle_width_limit > 0.0F) {
            subtitle_width_limit -= 52.0F;
            if (tray_width > 0.0F) {
                subtitle_width_limit -= tray_width + 28.0F;
            }
        }
        const auto subtitle_layout = showcase_detail::wrap_text_lines(
            subtitle_,
            [&](std::string_view line) { return measure_text(line, subtitle_font()); },
            subtitle_width_limit,
            3.0F);
        const float left_width = std::max(title_size.width, subtitle_layout.width);
        const float left_height = title_size.height + 10.0F + subtitle_layout.height;
        const float content_width = left_width + (tray_width > 0.0F ? tray_width + 28.0F : 0.0F);
        const float content_height = std::max(left_height, tray_height);
        return {
            480.0F,
            min_height_,
            std::max(660.0F, content_width + 52.0F),
            std::max(natural_height_, content_height + 46.0F),
        };
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (pills_.empty()) {
            return;
        }

        std::vector<nk::SizeRequest> requests;
        requests.reserve(pills_.size());
        float pills_width = 0.0F;
        float pills_height = 0.0F;
        for (const auto& pill : pills_) {
            if (!pill) {
                requests.push_back({});
                continue;
            }
            const auto req = pill->measure(nk::Constraints::tight(allocation.size()));
            requests.push_back(req);
            pills_width += req.natural_width;
            pills_height = std::max(pills_height, req.natural_height);
        }
        if (pills_.size() > 1) {
            pills_width += 10.0F * static_cast<float>(pills_.size() - 1);
        }

        float x = allocation.right() - 30.0F - pills_width;
        const float y =
            allocation.y + std::max(0.0F, (allocation.height - pills_height) * 0.5F) - 1.0F;
        for (std::size_t index = 0; index < pills_.size(); ++index) {
            if (!pills_[index]) {
                continue;
            }
            pills_[index]->allocate({
                x,
                y,
                requests[index].natural_width,
                requests[index].natural_height,
            });
            x += requests[index].natural_width + 10.0F;
        }
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const float corner_radius = 18.0F;
        const auto background = theme_color("surface-card", nk::Color{0.98F, 0.99F, 1.0F, 1.0F});
        const auto border = theme_color("border-color", nk::Color{0.85F, 0.88F, 0.92F, 1.0F});
        const auto accent = theme_color("accent-color", nk::Color{0.15F, 0.48F, 0.47F, 1.0F});
        const auto title_color = theme_color("text-color", nk::Color{0.12F, 0.14F, 0.17F, 1.0F});
        const auto subtitle_color = theme_color("text-color", nk::Color{0.36F, 0.4F, 0.45F, 1.0F});

        ctx.add_rounded_rect({a.x, a.y + 1.0F, a.width, a.height},
                             nk::Color{0.08F, 0.12F, 0.18F, 0.04F},
                             corner_radius + 1.0F);
        ctx.add_rounded_rect(a, background, corner_radius);
        ctx.add_border(a, border, 1.0F, corner_radius);
        ctx.add_rounded_rect({a.x + 22.0F, a.y + 18.0F, 54.0F, 4.0F},
                             nk::Color{accent.r, accent.g, accent.b, 0.18F},
                             2.0F);

        if (!pills_.empty()) {
            bool have_bounds = false;
            nk::Rect pill_bounds{};
            for (const auto& pill : pills_) {
                if (!pill) {
                    continue;
                }
                const auto pill_rect = pill->allocation();
                if (!have_bounds) {
                    pill_bounds = pill_rect;
                    have_bounds = true;
                    continue;
                }
                const float left = std::min(pill_bounds.x, pill_rect.x);
                const float top = std::min(pill_bounds.y, pill_rect.y);
                const float right = std::max(pill_bounds.right(), pill_rect.right());
                const float bottom = std::max(pill_bounds.bottom(), pill_rect.bottom());
                pill_bounds = {left, top, right - left, bottom - top};
            }

            if (have_bounds) {
                nk::Rect tray = {
                    pill_bounds.x - 8.0F,
                    pill_bounds.y - 6.0F,
                    pill_bounds.width + 16.0F,
                    pill_bounds.height + 12.0F,
                };
                const auto tray_fill = nk::Color{accent.r, accent.g, accent.b, 0.06F};
                ctx.add_rounded_rect(tray, tray_fill, tray.height * 0.5F);
                ctx.add_border(
                    tray, nk::Color{accent.r, accent.g, accent.b, 0.14F}, 1.0F, tray.height * 0.5F);
            }
        }

        if (!cached_title_size_) {
            cached_title_size_ = measure_text(title_, title_font());
        }
        const auto title_size = *cached_title_size_;
        float tray_width = 0.0F;
        for (std::size_t index = 0; index < pills_.size(); ++index) {
            if (!pills_[index]) {
                continue;
            }
            tray_width += pills_[index]->allocation().width;
            if (index + 1 < pills_.size()) {
                tray_width += 10.0F;
            }
        }
        float subtitle_width_limit = a.width - 52.0F;
        if (tray_width > 0.0F) {
            subtitle_width_limit -= tray_width + 28.0F;
        }
        const auto subtitle_layout = showcase_detail::wrap_text_lines(
            subtitle_,
            [&](std::string_view line) { return measure_text(line, subtitle_font()); },
            subtitle_width_limit,
            3.0F);
        const float text_block_height = title_size.height + 10.0F + subtitle_layout.height;
        const float title_x = a.x + 22.0F;
        const float title_y = a.y + std::max(0.0F, (a.height - text_block_height) * 0.5F);
        float subtitle_y = title_y + title_size.height + 10.0F;
        ctx.add_text({title_x, title_y}, title_, title_color, title_font());
        for (const auto& line : subtitle_layout.lines) {
            ctx.add_text({title_x, subtitle_y}, line, subtitle_color, subtitle_font());
            subtitle_y += subtitle_layout.line_height + 3.0F;
        }

        Widget::snapshot(ctx);
    }

private:
    HeroBanner() = default;

    static nk::FontDescriptor title_font() {
        return {
            .family = {},
            .size = 26.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    static nk::FontDescriptor subtitle_font() {
        return {
            .family = {},
            .size = 14.0F,
            .weight = nk::FontWeight::Regular,
        };
    }

    std::string title_;
    std::string subtitle_;
    std::vector<std::shared_ptr<nk::Widget>> pills_;
    mutable std::optional<nk::Size> cached_title_size_;
    float min_height_ = 120.0F;
    float natural_height_ = 132.0F;
};

class ShowcaseShell : public nk::Widget {
public:
    static std::shared_ptr<ShowcaseShell> create(std::shared_ptr<nk::Widget> menu_bar,
                                                 std::shared_ptr<nk::Widget> body,
                                                 std::shared_ptr<nk::Widget> status_bar) {
        auto shell = std::shared_ptr<ShowcaseShell>(new ShowcaseShell());
        shell->set_menu_bar(std::move(menu_bar));
        shell->set_body(std::move(body));
        shell->set_status_bar(std::move(status_bar));
        return shell;
    }

    void set_menu_bar(std::shared_ptr<nk::Widget> menu_bar) {
        replace_child(menu_bar_, menu_bar);
        menu_bar_ = std::move(menu_bar);
    }

    void set_body(std::shared_ptr<nk::Widget> body) {
        replace_child(body_, body);
        body_ = std::move(body);
    }

    void set_status_bar(std::shared_ptr<nk::Widget> status_bar) {
        replace_child(status_bar_, status_bar);
        status_bar_ = std::move(status_bar);
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        const auto menu_req = menu_bar_ ? menu_bar_->measure(constraints) : nk::SizeRequest{};
        const auto body_req = body_ ? body_->measure(constraints) : nk::SizeRequest{};
        const auto status_req = status_bar_ ? status_bar_->measure(constraints) : nk::SizeRequest{};

        return {
            std::max({menu_req.minimum_width, body_req.minimum_width, status_req.minimum_width}),
            menu_req.minimum_height + body_req.minimum_height + status_req.minimum_height,
            std::max({menu_req.natural_width, body_req.natural_width, status_req.natural_width}),
            menu_req.natural_height + body_req.natural_height + status_req.natural_height,
        };
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);

        const float top_height =
            menu_bar_ ? menu_bar_->measure(nk::Constraints::tight(allocation.size())).natural_height
                      : 0.0F;
        const float bottom_height =
            status_bar_
                ? status_bar_->measure(nk::Constraints::tight(allocation.size())).natural_height
                : 0.0F;
        const float middle_height = std::max(0.0F, allocation.height - top_height - bottom_height);

        if (menu_bar_) {
            menu_bar_->allocate({allocation.x, allocation.y, allocation.width, top_height});
        }
        if (body_) {
            body_->allocate(
                {allocation.x, allocation.y + top_height, allocation.width, middle_height});
        }
        if (status_bar_) {
            status_bar_->allocate({
                allocation.x,
                allocation.bottom() - bottom_height,
                allocation.width,
                bottom_height,
            });
        }
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        ctx.add_color_rect(a, theme_color("window-bg", nk::Color{0.97F, 0.97F, 0.98F, 1.0F}));

        if (menu_bar_) {
            const auto top = menu_bar_->allocation();
            ctx.add_color_rect(top,
                               theme_color("surface-panel", nk::Color{0.95F, 0.96F, 0.98F, 1.0F}));
            ctx.add_color_rect({top.x, top.bottom(), top.width, 1.0F},
                               theme_color("border-subtle", nk::Color{0.86F, 0.88F, 0.91F, 1.0F}));
        }

        if (status_bar_) {
            const auto bottom = status_bar_->allocation();
            ctx.add_color_rect(bottom,
                               theme_color("surface-panel", nk::Color{0.95F, 0.96F, 0.98F, 1.0F}));
            ctx.add_color_rect({bottom.x, bottom.y, bottom.width, 1.0F},
                               theme_color("border-subtle", nk::Color{0.86F, 0.88F, 0.91F, 1.0F}));
        }

        Widget::snapshot(ctx);
    }

private:
    ShowcaseShell() = default;

    void replace_child(const std::shared_ptr<nk::Widget>& old_child,
                       const std::shared_ptr<nk::Widget>& new_child) {
        if (old_child) {
            remove_child(*old_child);
        }
        if (new_child) {
            append_child(new_child);
        }
    }

    std::shared_ptr<nk::Widget> menu_bar_;
    std::shared_ptr<nk::Widget> body_;
    std::shared_ptr<nk::Widget> status_bar_;
};

class SplitColumns : public nk::Widget {
public:
    static std::shared_ptr<SplitColumns> create(std::shared_ptr<nk::Widget> left,
                                                std::shared_ptr<nk::Widget> right,
                                                float split_ratio = 0.45F,
                                                float spacing = 24.0F) {
        auto columns = std::shared_ptr<SplitColumns>(new SplitColumns());
        columns->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        columns->set_vertical_size_policy(nk::SizePolicy::Expanding);
        columns->set_vertical_stretch(1);
        columns->set_left(std::move(left));
        columns->set_right(std::move(right));
        columns->split_ratio_ = split_ratio;
        columns->spacing_ = spacing;
        return columns;
    }

    void set_left(std::shared_ptr<nk::Widget> left) {
        replace_child(left_, left);
        left_ = std::move(left);
    }

    void set_right(std::shared_ptr<nk::Widget> right) {
        replace_child(right_, right);
        right_ = std::move(right);
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        const auto left_req = left_ ? left_->measure(constraints) : nk::SizeRequest{};
        const auto right_req = right_ ? right_->measure(constraints) : nk::SizeRequest{};

        return {
            left_req.minimum_width + right_req.minimum_width + spacing_,
            std::max(left_req.minimum_height, right_req.minimum_height),
            left_req.natural_width + right_req.natural_width + spacing_,
            std::max(left_req.natural_height, right_req.natural_height),
        };
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (!left_ || !right_) {
            return;
        }

        const auto left_req = left_->measure(nk::Constraints::tight(allocation.size()));
        const auto right_req = right_->measure(nk::Constraints::tight(allocation.size()));

        const float available_width = std::max(0.0F, allocation.width - spacing_);
        const float min_left = std::min(left_req.minimum_width, available_width);
        const float min_right = std::min(right_req.minimum_width, available_width);
        const float desired_left = available_width * split_ratio_;
        const float max_left = std::max(min_left, available_width - min_right);
        const float left_width = std::clamp(desired_left, min_left, max_left);
        const float right_width = std::max(0.0F, available_width - left_width);

        left_->allocate({allocation.x, allocation.y, left_width, allocation.height});
        right_->allocate({
            allocation.x + left_width + spacing_,
            allocation.y,
            right_width,
            allocation.height,
        });
    }

private:
    SplitColumns() = default;

    void replace_child(const std::shared_ptr<nk::Widget>& old_child,
                       const std::shared_ptr<nk::Widget>& new_child) {
        if (old_child) {
            remove_child(*old_child);
        }
        if (new_child) {
            append_child(new_child);
        }
    }

    std::shared_ptr<nk::Widget> left_;
    std::shared_ptr<nk::Widget> right_;
    float split_ratio_ = 0.45F;
    float spacing_ = 24.0F;
};
