/// @file showcase.cpp
/// @brief Desktop-style showcase window for NodalKit widgets and runtime features.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <nk/foundation/property.h>
#include <nk/layout/box_layout.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <optional>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/image_view.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>
#include <string>
#include <utility>
#include <vector>

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
            cached_size_.reset();
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
            cached_size_.reset();
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float height = std::max(20.0F, cached_size_->height);
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
                     theme_color("text-color", nk::Color{0.40F, 0.44F, 0.49F, 1.0F}),
                     font_descriptor());
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

    std::string text_;
    mutable std::optional<nk::Size> cached_size_;
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

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        const float padding = theme_number("padding", 16.0F);
        if (!content_) {
            return {padding * 2.0F, padding * 2.0F, padding * 2.0F, padding * 2.0F};
        }

        const auto req = content_->measure(constraints);
        return {
            req.minimum_width + (padding * 2.0F),
            req.minimum_height + (padding * 2.0F),
            req.natural_width + (padding * 2.0F),
            req.natural_height + (padding * 2.0F),
        };
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (!content_) {
            return;
        }

        const float padding = theme_number("padding", 16.0F);
        const float inset = bordered_ ? 1.0F : 0.0F;
        content_->allocate({
            allocation.x + padding + inset,
            allocation.y + padding + inset,
            std::max(0.0F, allocation.width - ((padding + inset) * 2.0F)),
            std::max(0.0F, allocation.height - ((padding + inset) * 2.0F)),
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

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        if (!cached_title_size_) cached_title_size_ = measure_text(title_, title_font());
        if (!cached_subtitle_size_) cached_subtitle_size_ = measure_text(subtitle_, subtitle_font());
        const auto title_size = *cached_title_size_;
        const auto subtitle_size = *cached_subtitle_size_;
        const float left_width = std::max(title_size.width, subtitle_size.width);
        const float left_height = title_size.height + 10.0F + subtitle_size.height;

        float pills_width = 0.0F;
        float pills_height = 0.0F;
        for (std::size_t index = 0; index < pills_.size(); ++index) {
            if (!pills_[index]) {
                continue;
            }
            const auto req = pills_[index]->measure(constraints);
            pills_width += req.natural_width;
            pills_height = std::max(pills_height, req.natural_height);
            if (index + 1 < pills_.size()) {
                pills_width += 10.0F;
            }
        }

        const float content_width = left_width + (pills_width > 0.0F ? pills_width + 28.0F : 0.0F);
        const float content_height = std::max(left_height, pills_height);
        return {
            480.0F,
            120.0F,
            std::max(660.0F, content_width + 52.0F),
            std::max(132.0F, content_height + 46.0F),
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
        const float y = allocation.y + std::max(0.0F, (allocation.height - pills_height) * 0.5F);
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
                    pill_bounds.x - 10.0F,
                    pill_bounds.y - 8.0F,
                    pill_bounds.width + 20.0F,
                    pill_bounds.height + 16.0F,
                };
                ctx.add_rounded_rect(
                    tray, nk::Color{0.95F, 0.97F, 0.985F, 1.0F}, tray.height * 0.5F);
                ctx.add_border(tray,
                               theme_color("border-color", nk::Color{0.86F, 0.89F, 0.92F, 1.0F}),
                               1.0F,
                               tray.height * 0.5F);
            }
        }

        if (!cached_title_size_) cached_title_size_ = measure_text(title_, title_font());
        if (!cached_subtitle_size_) cached_subtitle_size_ = measure_text(subtitle_, subtitle_font());
        const auto title_size = *cached_title_size_;
        const auto subtitle_size = *cached_subtitle_size_;
        const float text_block_height = title_size.height + 10.0F + subtitle_size.height;
        const float title_x = a.x + 22.0F;
        const float title_y = a.y + std::max(0.0F, (a.height - text_block_height) * 0.5F);
        const float subtitle_y = title_y + title_size.height + 10.0F;
        ctx.add_text({title_x, title_y}, title_, title_color, title_font());
        ctx.add_text({title_x, subtitle_y}, subtitle_, subtitle_color, subtitle_font());

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
    mutable std::optional<nk::Size> cached_subtitle_size_;
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

static std::vector<uint32_t> generate_test_pattern(int width, int height, int frame) {
    std::vector<uint32_t> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto r = static_cast<uint8_t>((x + frame) % 256);
            const auto g = static_cast<uint8_t>((y + frame / 2) % 256);
            const auto b = static_cast<uint8_t>(((x + y) + frame) % 256);
            pixels[static_cast<std::size_t>(y * width + x)] =
                0xFF000000U | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
                static_cast<uint32_t>(b);
        }
    }
    return pixels;
}

int main(int argc, char** argv) {
    nk::Application app(argc, argv);

    nk::ThemeSelection theme_selection;
    theme_selection.family = nk::ThemeFamily::LinuxGnome;
    theme_selection.density = nk::ThemeDensity::Comfortable;
    theme_selection.accent_color_override = nk::Color::from_rgb(38, 132, 131);
    app.set_theme_selection(theme_selection);

    nk::Window window({
        .title = "NodalKit Showcase",
        .width = 1180,
        .height = 760,
    });

    int counter = 0;
    int frame_number = 0;

    auto menu_bar = nk::MenuBar::create();
    menu_bar->add_menu({
        "File",
        {
            nk::MenuItem::action("New", "file.new"),
            nk::MenuItem::action("Open...", "file.open"),
            nk::MenuItem::make_separator(),
            nk::MenuItem::action("Quit", "file.quit"),
        },
    });
    menu_bar->add_menu({
        "Edit",
        {
            nk::MenuItem::action("Undo", "edit.undo"),
            nk::MenuItem::action("Redo", "edit.redo"),
            nk::MenuItem::make_separator(),
            nk::MenuItem::action("Cut", "edit.cut"),
            nk::MenuItem::action("Copy", "edit.copy"),
            nk::MenuItem::action("Paste", "edit.paste"),
        },
    });
    menu_bar->add_menu({
        "View",
        {
            nk::MenuItem::action("Zoom In", "view.zoom_in"),
            nk::MenuItem::action("Zoom Out", "view.zoom_out"),
            nk::MenuItem::action("Reset Zoom", "view.zoom_reset"),
        },
    });
    menu_bar->add_menu({
        "Help",
        {
            nk::MenuItem::action("About NodalKit...", "help.about"),
        },
    });

    auto status_bar = nk::StatusBar::create();
    status_bar->set_segments({"Ready", "10 items", "Counter 0"});

    auto hero_counter_pill = StatusPill::create("Counter 0", true);
    auto hero_items_pill = StatusPill::create("10 items");
    auto hero_preview_pill = StatusPill::create("Nearest");

    auto counter_label = ValueText::create("Counter 0");
    auto increment_btn = nk::Button::create("Increment");
    increment_btn->add_style_class("suggested");
    auto decrement_btn = nk::Button::create("Decrement");
    auto reset_btn = nk::Button::create("Reset");

    (void)increment_btn->on_clicked().connect([&] {
        ++counter;
        counter_label->set_text("Counter " + std::to_string(counter));
        status_bar->set_segment(2, "Counter " + std::to_string(counter));
        hero_counter_pill->set_text("Counter " + std::to_string(counter));
    });

    (void)decrement_btn->on_clicked().connect([&] {
        --counter;
        counter_label->set_text("Counter " + std::to_string(counter));
        status_bar->set_segment(2, "Counter " + std::to_string(counter));
        hero_counter_pill->set_text("Counter " + std::to_string(counter));
    });

    (void)reset_btn->on_clicked().connect([&] {
        counter = 0;
        counter_label->set_text("Counter 0");
        status_bar->set_segment(2, "Counter 0");
        hero_counter_pill->set_text("Counter 0");
    });

    auto primary_counter_actions = Box::horizontal(8.0F);
    primary_counter_actions->append(increment_btn);
    primary_counter_actions->append(decrement_btn);
    primary_counter_actions->set_horizontal_size_policy(nk::SizePolicy::Preferred);
    auto button_row = Box::horizontal(16.0F);
    button_row->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    button_row->append(primary_counter_actions);
    button_row->append(Spacer::create());
    button_row->append(reset_btn);

    auto input_title = SectionTitle::create("Command Workspace");
    auto input_subtitle =
        SecondaryText::create("Primary actions, short command entry, and accent selection.");
    auto command_label = FieldLabel::create("Command");
    auto text_field = nk::TextField::create();
    text_field->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    text_field->set_placeholder("Type a short command...");
    auto echo_result = SecondaryText::create("Echo will appear here.");
    auto counter_caption = FieldLabel::create("Counter");
    auto combo_label = FieldLabel::create("Accent preset");
    auto combo = nk::ComboBox::create();
    combo->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    combo->set_items({"Red", "Green", "Blue", "Yellow", "Cyan", "Magenta"});
    combo->set_selected_index(0);
    auto combo_result = SecondaryText::create("Selected: Red");

    (void)text_field->on_text_changed().connect([&](std::string_view text) {
        echo_result->set_text(text.empty() ? "Echo will appear here."
                                           : "Echo: " + std::string(text));
    });

    (void)text_field->on_activate().connect([&] {
        echo_result->set_text("Submitted: " + std::string(text_field->text()));
        status_bar->set_segment(0, "Submitted command");
    });

    (void)combo->on_selection_changed().connect([&](int index) {
        if (index >= 0) {
            combo_result->set_text("Selected: " +
                                   std::string(combo->item(static_cast<std::size_t>(index))));
        } else {
            combo_result->set_text("Selected: (none)");
        }
    });

    auto command_group = Box::vertical(10.0F);
    command_group->append(command_label);
    command_group->append(text_field);
    command_group->append(echo_result);

    auto counter_group = Box::vertical(10.0F);
    counter_group->append(counter_caption);
    counter_group->append(counter_label);
    counter_group->append(button_row);

    auto accent_group = Box::vertical(10.0F);
    accent_group->append(combo_label);
    accent_group->append(combo);
    accent_group->append(combo_result);

    auto controls_content = Box::vertical(18.0F);
    controls_content->append(input_title);
    controls_content->append(input_subtitle);
    controls_content->append(InsetStage::create(command_group, 106.0F, 112.0F, 14.0F));
    controls_content->append(InsetStage::create(counter_group, 124.0F, 132.0F, 14.0F));
    controls_content->append(InsetStage::create(accent_group, 110.0F, 116.0F, 14.0F));
    auto controls_card = SurfacePanel::card(controls_content);

    auto list_title = SectionTitle::create("List & Selection");
    auto list_subtitle =
        SecondaryText::create("A bounded viewport keeps selection and insertion behavior visible.");
    auto model = std::make_shared<nk::StringListModel>(std::vector<std::string>{
        "Alpha",
        "Bravo",
        "Charlie",
        "Delta",
        "Echo",
        "Foxtrot",
        "Golf",
        "Hotel",
        "India",
        "Juliet",
    });
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    selection->select(0);
    auto list_view = nk::ListView::create();
    list_view->set_model(model);
    list_view->set_selection_model(selection);
    list_view->set_row_height(30.0F);
    auto list_stage = InsetStage::create(list_view, 220.0F, 220.0F, 4.0F);
    auto list_status = StatusPill::create("10 items");
    auto add_item_btn = nk::Button::create("Add Item");
    add_item_btn->add_style_class("suggested");

    (void)add_item_btn->on_clicked().connect([&] {
        const auto next = model->row_count() + 1;
        model->append("Item " + std::to_string(next));
        const auto count_text = std::to_string(model->row_count()) + " items";
        list_status->set_text(count_text);
        status_bar->set_segment(1, count_text);
        hero_items_pill->set_text(count_text);
    });

    auto list_footer = Box::horizontal(10.0F);
    list_footer->append(list_status);
    list_footer->append(add_item_btn);

    auto list_content = Box::vertical(12.0F);
    list_content->append(list_title);
    list_content->append(list_subtitle);
    list_content->append(list_stage);
    list_content->append(list_footer);
    auto list_card = SurfacePanel::card(list_content);

    auto preview_title = SectionTitle::create("Preview Stage");
    auto preview_subtitle = SecondaryText::create(
        "Live raster output with deliberate surface priority and explicit scaling.");
    auto image_label = FieldLabel::create("Live image");
    auto image_meta = ValueText::create("128 x 96 source");
    auto image_detail = SecondaryText::create("Animated sample with explicit display scaling.");
    auto preview_canvas = PreviewCanvas::create();
    auto preview_stage = InsetStage::create(preview_canvas, 348.0F, 372.0F, 6.0F);
    auto scale_label = FieldLabel::create("Scale mode");
    auto scale_combo = nk::ComboBox::create();
    scale_combo->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    scale_combo->set_items({"Nearest Neighbor", "Bilinear"});
    scale_combo->set_selected_index(0);
    auto scale_state = SecondaryText::create("Nearest-neighbor scaling active.");

    constexpr int kImageWidth = 128;
    constexpr int kImageHeight = 96;
    auto pixels = generate_test_pattern(kImageWidth, kImageHeight, 0);
    preview_canvas->update_pixel_buffer(pixels.data(), kImageWidth, kImageHeight);

    (void)scale_combo->on_selection_changed().connect([&](int index) {
        preview_canvas->set_scale_mode(index == 0 ? nk::ScaleMode::NearestNeighbor
                                                  : nk::ScaleMode::Bilinear);
        scale_state->set_text(index == 0 ? "Nearest-neighbor scaling active."
                                         : "Bilinear scaling active.");
        hero_preview_pill->set_text(index == 0 ? "Nearest" : "Bilinear");
    });

    auto preview_info = Box::vertical(10.0F);
    preview_info->append(image_label);
    preview_info->append(image_meta);
    preview_info->append(image_detail);
    preview_info->append(scale_label);
    preview_info->append(scale_combo);
    preview_info->append(scale_state);
    preview_info->append(Spacer::create());

    auto preview_display = SplitColumns::create(preview_stage, preview_info, 0.80F, 16.0F);

    auto preview_content = Box::vertical(16.0F);
    preview_content->append(preview_title);
    preview_content->append(preview_subtitle);
    preview_content->append(preview_display);
    auto preview_card = SurfacePanel::card(preview_content);

    auto actions_title = SectionTitle::create("Runtime Actions");
    auto actions_subtitle =
        SecondaryText::create("Property binding, dialog flow, and command feedback.");
    auto prop_label = FieldLabel::create("Property binding");
    nk::Property<int> source_prop{42};
    nk::Property<int> target_prop{0};
    [[maybe_unused]] auto binding = target_prop.bind_to(source_prop);
    auto prop_value_label =
        ValueText::create("Source: 42, Target: " + std::to_string(target_prop.get()));
    auto prop_detail = SecondaryText::create("Shared state mirrors into the status bar.");
    auto prop_btn = nk::Button::create("Set Source = 99");
    auto dialog_label = FieldLabel::create("Dialog flow");
    auto dialog_detail = SecondaryText::create("Open a modal and report the result.");
    auto dialog_btn = nk::Button::create("Show Dialog");
    dialog_btn->add_style_class("suggested");
    auto runtime_status = ValueText::create("Waiting for an action.");

    (void)prop_btn->on_clicked().connect([&] {
        source_prop.set(99);
        prop_value_label->set_text("Source: " + std::to_string(source_prop.get()) +
                                   ", Target: " + std::to_string(target_prop.get()));
        runtime_status->set_text("Property binding updated.");
        status_bar->set_segment(0, "Property binding updated");
    });

    (void)dialog_btn->on_clicked().connect([&] {
        auto dialog = nk::Dialog::create("Confirmation", "Do you want to continue?");
        dialog->add_button("Cancel", nk::DialogResponse::Cancel);
        dialog->add_button("OK", nk::DialogResponse::Accept);
        (void)dialog->on_response().connect([&](nk::DialogResponse response) {
            if (response == nk::DialogResponse::Accept) {
                status_bar->set_segment(0, "Dialog: Accepted");
                runtime_status->set_text("Dialog accepted.");
            } else {
                status_bar->set_segment(0, "Dialog: Cancelled");
                runtime_status->set_text("Dialog cancelled.");
            }
        });
        dialog->present(window);
    });

    auto property_group = Box::vertical(10.0F);
    property_group->append(prop_label);
    property_group->append(prop_value_label);
    property_group->append(prop_detail);
    property_group->append(prop_btn);
    auto property_panel = InsetStage::create(property_group, 138.0F, 146.0F, 14.0F);

    auto dialog_group = Box::vertical(10.0F);
    dialog_group->append(dialog_label);
    dialog_group->append(dialog_detail);
    dialog_group->append(dialog_btn);
    auto dialog_panel = InsetStage::create(dialog_group, 126.0F, 134.0F, 14.0F);

    auto status_label = FieldLabel::create("Runtime status");
    auto status_group = Box::vertical(8.0F);
    status_group->append(status_label);
    status_group->append(runtime_status);
    auto status_panel = InsetStage::create(status_group, 80.0F, 88.0F, 14.0F);

    auto actions_content = Box::vertical(16.0F);
    actions_content->append(actions_title);
    actions_content->append(actions_subtitle);
    actions_content->append(property_panel);
    actions_content->append(dialog_panel);
    actions_content->append(status_panel);
    auto actions_card = SurfacePanel::card(actions_content);

    auto left_column = Box::vertical(18.0F);
    left_column->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    left_column->set_vertical_size_policy(nk::SizePolicy::Expanding);
    left_column->append(controls_card);
    left_column->append(list_card);

    auto right_column = Box::vertical(18.0F);
    right_column->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    right_column->set_vertical_size_policy(nk::SizePolicy::Expanding);
    right_column->append(preview_card);
    right_column->append(actions_card);

    preview_card->set_vertical_size_policy(nk::SizePolicy::Expanding);
    preview_card->set_vertical_stretch(2);
    actions_card->set_vertical_size_policy(nk::SizePolicy::Expanding);
    actions_card->set_vertical_stretch(1);

    auto content_row = SplitColumns::create(left_column, right_column, 0.45F, 24.0F);
    auto hero_banner = HeroBanner::create(
        "NodalKit Desktop Showcase",
        "A compact workspace for inputs, model/view state, and live raster output.",
        {hero_counter_pill, hero_items_pill, hero_preview_pill});

    auto body_content = Box::vertical(20.0F);
    body_content->append(hero_banner);
    body_content->append(content_row);

    auto body_page = SurfacePanel::page(body_content);
    auto root = ShowcaseShell::create(menu_bar, body_page, status_bar);
    window.set_child(root);

    list_view->grab_focus();

    (void)menu_bar->on_action().connect([&](std::string_view action) {
        if (action == "file.quit") {
            app.quit(0);
            return;
        }

        if (action == "help.about") {
            auto dialog = nk::Dialog::create(
                "About", "NodalKit Showcase v0.1.0\nA C++23 desktop GUI toolkit");
            dialog->add_button("OK", nk::DialogResponse::Accept);
            dialog->present(window);
            return;
        }

        status_bar->set_segment(0, "Action: " + std::string(action));
        runtime_status->set_text("Menu action: " + std::string(action));
    });

    (void)app.event_loop().set_interval(std::chrono::milliseconds(33), [&] {
        ++frame_number;
        auto frame = generate_test_pattern(kImageWidth, kImageHeight, frame_number);
        preview_canvas->update_pixel_buffer(frame.data(), kImageWidth, kImageHeight);
    });

    (void)window.on_close_request().connect([&] { app.quit(0); });

    window.present();
    return app.run();
}
