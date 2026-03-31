/// @file showcase.cpp
/// @brief Demonstrates all available NodalKit widgets and features.
///
/// Layout:
///   ┌─────────────────────────────────────────────┐
///   │ MenuBar: File | Edit | View | Help          │
///   ├─────────────────────────────────────────────┤
///   │  ┌─── Left Panel ────┐ ┌── Right Panel ──┐ │
///   │  │ Label              │ │ ImageView       │ │
///   │  │ Button (counter)   │ │ (test pattern)  │ │
///   │  │ TextField          │ │                 │ │
///   │  │ ComboBox           │ │                 │ │
///   │  │ ListView           │ │                 │ │
///   │  └────────────────────┘ └─────────────────┘ │
///   ├─────────────────────────────────────────────┤
///   │ StatusBar: Ready | Items: 5 | Counter: 0    │
///   └─────────────────────────────────────────────┘

#include <nk/foundation/property.h>
#include <nk/foundation/signal.h>
#include <nk/layout/box_layout.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/render/image_node.h>
#include <nk/render/snapshot_context.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/image_view.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helper: a reusable Box container with BoxLayout
// ---------------------------------------------------------------------------
class Box : public nk::Widget {
public:
    static std::shared_ptr<Box> vertical(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto lm = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
        lm->set_spacing(spacing);
        box->set_layout_manager(std::move(lm));
        return box;
    }

    static std::shared_ptr<Box> horizontal(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto lm = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
        lm->set_spacing(spacing);
        box->set_layout_manager(std::move(lm));
        return box;
    }

    void append(std::shared_ptr<nk::Widget> child) {
        append_child(std::move(child));
    }

    void set_homogeneous(bool homogeneous) {
        if (auto* layout = dynamic_cast<nk::BoxLayout*>(layout_manager())) {
            layout->set_homogeneous(homogeneous);
            queue_layout();
        }
    }

private:
    Box() = default;
};

class SurfacePanel : public nk::Widget {
public:
    static std::shared_ptr<SurfacePanel> page(
        std::shared_ptr<nk::Widget> content) {
        auto panel = std::shared_ptr<SurfacePanel>(new SurfacePanel(false));
        panel->add_style_class("page");
        panel->set_content(std::move(content));
        return panel;
    }

    static std::shared_ptr<SurfacePanel> card(
        std::shared_ptr<nk::Widget> content) {
        auto panel = std::shared_ptr<SurfacePanel>(new SurfacePanel(true));
        panel->add_style_class("card");
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

    [[nodiscard]] nk::SizeRequest measure(
        nk::Constraints const& constraints) const override {
        float const padding = theme_number("padding", 16.0F);
        if (!content_) {
            return {padding * 2.0F, padding * 2.0F,
                    padding * 2.0F, padding * 2.0F};
        }
        auto const req = content_->measure(constraints);
        return {
            req.minimum_width + (padding * 2.0F),
            req.minimum_height + (padding * 2.0F),
            req.natural_width + (padding * 2.0F),
            req.natural_height + (padding * 2.0F),
        };
    }

    void allocate(nk::Rect const& allocation) override {
        Widget::allocate(allocation);
        if (!content_) {
            return;
        }
        float const padding = theme_number("padding", 16.0F);
        float const inset = bordered_ ? 1.0F : 0.0F;
        content_->allocate({
            allocation.x + padding + inset,
            allocation.y + padding + inset,
            std::max(0.0F, allocation.width - ((padding + inset) * 2.0F)),
            std::max(0.0F, allocation.height - ((padding + inset) * 2.0F)),
        });
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        auto const a = allocation();
        auto const background =
            theme_color("background", nk::Color{1.0F, 1.0F, 1.0F, 1.0F});
        float const corner_radius = theme_number("corner-radius", 12.0F);

        if (bordered_) {
            ctx.add_rounded_rect(
                {a.x, a.y + 1.0F, a.width, a.height},
                nk::Color{0.08F, 0.12F, 0.18F, 0.04F},
                corner_radius + 1.0F);
            ctx.add_rounded_rect(a, background, corner_radius);
            ctx.add_border(
                a,
                theme_color("border-color",
                            nk::Color{0.86F, 0.88F, 0.91F, 1.0F}),
                1.0F,
                corner_radius);
        } else {
            ctx.add_color_rect(a, background);
        }

        Widget::snapshot(ctx);
    }

private:
    explicit SurfacePanel(bool bordered)
        : bordered_(bordered) {}

    std::shared_ptr<nk::Widget> content_;
    bool bordered_ = false;
};

class ShowcaseShell : public nk::Widget {
public:
    static std::shared_ptr<ShowcaseShell> create(
        std::shared_ptr<nk::Widget> menu_bar,
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

    [[nodiscard]] nk::SizeRequest measure(
        nk::Constraints const& constraints) const override {
        auto const menu_req = menu_bar_
            ? menu_bar_->measure(constraints) : nk::SizeRequest{};
        auto const body_req = body_
            ? body_->measure(constraints) : nk::SizeRequest{};
        auto const status_req = status_bar_
            ? status_bar_->measure(constraints) : nk::SizeRequest{};

        return {
            std::max({menu_req.minimum_width, body_req.minimum_width,
                      status_req.minimum_width}),
            menu_req.minimum_height + body_req.minimum_height
                + status_req.minimum_height,
            std::max({menu_req.natural_width, body_req.natural_width,
                      status_req.natural_width}),
            menu_req.natural_height + body_req.natural_height
                + status_req.natural_height,
        };
    }

    void allocate(nk::Rect const& allocation) override {
        Widget::allocate(allocation);

        float top_height = menu_bar_
            ? menu_bar_->measure(nk::Constraints::tight(allocation.size()))
                  .natural_height
            : 0.0F;
        float bottom_height = status_bar_
            ? status_bar_->measure(nk::Constraints::tight(allocation.size()))
                  .natural_height
            : 0.0F;
        float middle_height =
            std::max(0.0F, allocation.height - top_height - bottom_height);

        if (menu_bar_) {
            menu_bar_->allocate(
                {allocation.x, allocation.y, allocation.width, top_height});
        }
        if (body_) {
            body_->allocate({allocation.x, allocation.y + top_height,
                             allocation.width, middle_height});
        }
        if (status_bar_) {
            status_bar_->allocate(
                {allocation.x, allocation.bottom() - bottom_height,
                 allocation.width, bottom_height});
        }
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        auto const a = allocation();
        ctx.add_color_rect(
            a, theme_color("window-bg", nk::Color{0.97F, 0.97F, 0.98F, 1.0F}));

        if (menu_bar_) {
            auto const top = menu_bar_->allocation();
            ctx.add_color_rect(
                top,
                theme_color("surface-panel", nk::Color{0.95F, 0.96F, 0.98F, 1.0F}));
            ctx.add_color_rect(
                {top.x, top.bottom(), top.width, 1.0F},
                theme_color("border-subtle", nk::Color{0.86F, 0.88F, 0.91F, 1.0F}));
        }

        if (status_bar_) {
            auto const bottom = status_bar_->allocation();
            ctx.add_color_rect(
                bottom,
                theme_color("surface-panel", nk::Color{0.95F, 0.96F, 0.98F, 1.0F}));
            ctx.add_color_rect(
                {bottom.x, bottom.y, bottom.width, 1.0F},
                theme_color("border-subtle", nk::Color{0.86F, 0.88F, 0.91F, 1.0F}));
        }

        Widget::snapshot(ctx);
    }

private:
    ShowcaseShell() = default;

    void replace_child(
        std::shared_ptr<nk::Widget> const& old_child,
        std::shared_ptr<nk::Widget> const& new_child) {
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
    static std::shared_ptr<SplitColumns> create(
        std::shared_ptr<nk::Widget> left,
        std::shared_ptr<nk::Widget> right,
        float split_ratio = 0.53F,
        float spacing = 24.0F) {
        auto columns = std::shared_ptr<SplitColumns>(new SplitColumns());
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

    [[nodiscard]] nk::SizeRequest measure(
        nk::Constraints const& constraints) const override {
        auto const left_req = left_
            ? left_->measure(constraints) : nk::SizeRequest{};
        auto const right_req = right_
            ? right_->measure(constraints) : nk::SizeRequest{};

        return {
            left_req.minimum_width + right_req.minimum_width + spacing_,
            std::max(left_req.minimum_height, right_req.minimum_height),
            left_req.natural_width + right_req.natural_width + spacing_,
            std::max(left_req.natural_height, right_req.natural_height),
        };
    }

    void allocate(nk::Rect const& allocation) override {
        Widget::allocate(allocation);

        if (!left_ || !right_) {
            return;
        }

        auto const left_req = left_->measure(
            nk::Constraints::tight(allocation.size()));
        auto const right_req = right_->measure(
            nk::Constraints::tight(allocation.size()));

        float const available_width = std::max(0.0F, allocation.width - spacing_);
        float const min_left = std::min(left_req.minimum_width, available_width);
        float const min_right = std::min(right_req.minimum_width, available_width);
        float const desired_left = available_width * split_ratio_;
        float const max_left = std::max(min_left, available_width - min_right);
        float const left_width = std::clamp(desired_left, min_left, max_left);
        float const right_width = std::max(0.0F, available_width - left_width);

        left_->allocate(
            {allocation.x, allocation.y, left_width, allocation.height});
        right_->allocate(
            {allocation.x + left_width + spacing_, allocation.y,
             right_width, allocation.height});
    }

private:
    SplitColumns() = default;

    void replace_child(
        std::shared_ptr<nk::Widget> const& old_child,
        std::shared_ptr<nk::Widget> const& new_child) {
        if (old_child) {
            remove_child(*old_child);
        }
        if (new_child) {
            append_child(new_child);
        }
    }

    std::shared_ptr<nk::Widget> left_;
    std::shared_ptr<nk::Widget> right_;
    float split_ratio_ = 0.53F;
    float spacing_ = 24.0F;
};

// ---------------------------------------------------------------------------
// Generate a colorful test pattern for the ImageView (ARGB8888)
// ---------------------------------------------------------------------------
static std::vector<uint32_t> generate_test_pattern(int w, int h, int frame) {
    std::vector<uint32_t> pixels(static_cast<std::size_t>(w * h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Gradient with animated offset.
            auto const r = static_cast<uint8_t>((x + frame) % 256);
            auto const g = static_cast<uint8_t>((y + frame / 2) % 256);
            auto const b = static_cast<uint8_t>(((x + y) + frame) % 256);
            pixels[static_cast<std::size_t>(y * w + x)] =
                0xFF000000U | (static_cast<uint32_t>(r) << 16)
                            | (static_cast<uint32_t>(g) << 8)
                            | static_cast<uint32_t>(b);
        }
    }
    return pixels;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    nk::Application app(argc, argv);
    nk::ThemeSelection theme_selection;
    theme_selection.family = nk::ThemeFamily::LinuxGnome;
    theme_selection.density = nk::ThemeDensity::Comfortable;
    theme_selection.accent_color_override =
        nk::Color::from_rgb(38, 132, 131);
    app.set_theme_selection(theme_selection);

    nk::Window window({
        .title = "NodalKit Showcase",
        .width = 1040,
        .height = 720,
    });

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    int counter = 0;
    int frame_number = 0;

    // -----------------------------------------------------------------------
    // Menu Bar
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // Status Bar
    // -----------------------------------------------------------------------
    auto status_bar = nk::StatusBar::create();
    status_bar->set_segments({"Ready", "10 items", "Counter 0"});

    // -----------------------------------------------------------------------
    // Controls card
    // -----------------------------------------------------------------------
    auto counter_label = nk::Label::create("Counter: 0");
    auto increment_btn = nk::Button::create("Increment");
    increment_btn->add_style_class("suggested");
    auto decrement_btn = nk::Button::create("Decrement");
    auto reset_btn = nk::Button::create("Reset");
    reset_btn->add_style_class("flat");

    (void)increment_btn->on_clicked().connect(
        [&counter, &counter_label, &status_bar] {
            ++counter;
            counter_label->set_text("Counter: " + std::to_string(counter));
            status_bar->set_segment(
                2, "Counter " + std::to_string(counter));
        });

    (void)decrement_btn->on_clicked().connect(
        [&counter, &counter_label, &status_bar] {
            --counter;
            counter_label->set_text("Counter: " + std::to_string(counter));
            status_bar->set_segment(
                2, "Counter " + std::to_string(counter));
        });

    (void)reset_btn->on_clicked().connect(
        [&counter, &counter_label, &status_bar] {
            counter = 0;
            counter_label->set_text("Counter: 0");
            status_bar->set_segment(2, "Counter 0");
        });

    auto button_row = Box::horizontal(8.0F);
    button_row->append(increment_btn);
    button_row->append(decrement_btn);
    button_row->append(reset_btn);

    auto input_title = nk::Label::create("Inputs");
    input_title->add_style_class("heading");
    auto input_subtitle = nk::Label::create(
        "Primary actions and text entry.");
    input_subtitle->add_style_class("muted");

    auto echo_label = nk::Label::create("Command text");
    echo_label->add_style_class("muted");
    auto text_field = nk::TextField::create();
    text_field->set_placeholder("Type a short command...");
    auto echo_result = nk::Label::create("Echo will appear here.");
    echo_result->add_style_class("muted");

    (void)text_field->on_text_changed().connect(
        [&echo_result](std::string_view const& text) {
            echo_result->set_text(
                text.empty() ? "Echo will appear here."
                             : "Echo: " + std::string(text));
        });

    (void)text_field->on_activate().connect([&echo_result, &text_field] {
        echo_result->set_text(
            "Submitted: " + std::string(text_field->text()));
    });

    auto combo_label = nk::Label::create("Accent preset");
    combo_label->add_style_class("muted");
    auto combo = nk::ComboBox::create();
    combo->set_items({"Red", "Green", "Blue", "Yellow", "Cyan", "Magenta"});
    combo->set_selected_index(0);
    auto combo_result = nk::Label::create("Selected: Red");
    combo_result->add_style_class("muted");

    (void)combo->on_selection_changed().connect(
        [&combo, &combo_result](int const& index) {
            if (index >= 0) {
                combo_result->set_text(
                    "Selected: " + std::string(combo->item(
                                       static_cast<std::size_t>(index))));
            } else {
                combo_result->set_text("Selected: (none)");
            }
        });

    auto controls_content = Box::vertical(12.0F);
    controls_content->append(input_title);
    controls_content->append(input_subtitle);
    controls_content->append(counter_label);
    controls_content->append(button_row);
    controls_content->append(echo_label);
    controls_content->append(text_field);
    controls_content->append(echo_result);
    controls_content->append(combo_label);
    controls_content->append(combo);
    controls_content->append(combo_result);
    auto controls_card = SurfacePanel::card(controls_content);

    // ListView backed by a StringListModel.
    auto list_title = nk::Label::create("List Model");
    list_title->add_style_class("heading");
    auto list_label = nk::Label::create(
        "Selection stays visible in a constrained viewport.");
    list_label->add_style_class("muted");
    auto model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{
            "Alpha", "Bravo", "Charlie", "Delta", "Echo",
            "Foxtrot", "Golf", "Hotel", "India", "Juliet",
        });
    auto selection = std::make_shared<nk::SelectionModel>(
        nk::SelectionMode::Single);
    selection->select(0);
    auto list_view = nk::ListView::create();
    list_view->set_model(model);
    list_view->set_selection_model(selection);
    list_view->set_row_height(30.0F);

    auto list_status = nk::Label::create("10 items");
    list_status->add_style_class("muted");

    // Add-item button.
    auto add_item_btn = nk::Button::create("Add Item");
    add_item_btn->add_style_class("suggested");
    (void)add_item_btn->on_clicked().connect(
        [&model, &list_status, &status_bar] {
            auto const n = model->row_count() + 1;
            model->append("Item " + std::to_string(n));
            list_status->set_text(std::to_string(model->row_count()) + " items");
            status_bar->set_segment(
                1, std::to_string(model->row_count()) + " items");
        });

    auto list_content = Box::vertical(12.0F);
    list_content->append(list_title);
    list_content->append(list_label);
    list_content->append(list_view);
    list_content->append(list_status);
    list_content->append(add_item_btn);
    auto list_card = SurfacePanel::card(list_content);

    // -----------------------------------------------------------------------
    // Preview card
    // -----------------------------------------------------------------------
    auto preview_title = nk::Label::create("Preview");
    preview_title->add_style_class("heading");
    auto preview_subtitle = nk::Label::create(
        "Live raster preview with explicit scaling.");
    preview_subtitle->add_style_class("muted");
    auto image_label = nk::Label::create("Live image");
    image_label->add_style_class("muted");
    auto image_meta = nk::Label::create("128 x 96 animated sample");
    image_meta->add_style_class("muted");
    auto image_view = nk::ImageView::create();
    image_view->set_scale_mode(nk::ScaleMode::NearestNeighbor);
    image_view->set_preserve_aspect_ratio(true);

    // Initial frame.
    constexpr int kImgW = 128;
    constexpr int kImgH = 96;
    auto pixels = generate_test_pattern(kImgW, kImgH, 0);
    image_view->update_pixel_buffer(pixels.data(), kImgW, kImgH);

    auto scale_label = nk::Label::create("Scale mode");
    scale_label->add_style_class("muted");
    auto scale_combo = nk::ComboBox::create();
    scale_combo->set_items({"Nearest Neighbor", "Bilinear"});
    scale_combo->set_selected_index(0);

    (void)scale_combo->on_selection_changed().connect(
        [&image_view](int const& idx) {
            image_view->set_scale_mode(
                idx == 0 ? nk::ScaleMode::NearestNeighbor
                         : nk::ScaleMode::Bilinear);
        });

    auto preview_content = Box::vertical(12.0F);
    preview_content->append(preview_title);
    preview_content->append(preview_subtitle);
    preview_content->append(image_label);
    preview_content->append(image_meta);
    preview_content->append(image_view);
    preview_content->append(scale_label);
    preview_content->append(scale_combo);
    auto preview_card = SurfacePanel::card(preview_content);

    // Property binding demo.
    auto actions_title = nk::Label::create("Runtime Signals");
    actions_title->add_style_class("heading");
    auto actions_subtitle = nk::Label::create(
        "Property binding and dialog flow.");
    actions_subtitle->add_style_class("muted");
    auto prop_label = nk::Label::create("Property binding");
    prop_label->add_style_class("muted");
    nk::Property<int> source_prop{42};
    nk::Property<int> target_prop{0};
    [[maybe_unused]] auto binding = target_prop.bind_to(source_prop);
    auto prop_value_label = nk::Label::create(
        "Source: 42, Target: " + std::to_string(target_prop.get()));
    prop_value_label->add_style_class("muted");

    auto prop_btn = nk::Button::create("Set Source = 99");
    (void)prop_btn->on_clicked().connect(
        [&source_prop, &target_prop, &prop_value_label] {
            source_prop.set(99);
            prop_value_label->set_text(
                "Source: " + std::to_string(source_prop.get())
                + ", Target: " + std::to_string(target_prop.get()));
        });

    // Dialog demo.
    auto dialog_label = nk::Label::create("Dialog flow");
    dialog_label->add_style_class("muted");
    auto dialog_btn = nk::Button::create("Show Dialog");
    dialog_btn->add_style_class("suggested");
    (void)dialog_btn->on_clicked().connect([&window, &status_bar] {
        auto dlg = nk::Dialog::create(
            "Confirmation", "Do you want to continue?");
        dlg->add_button("Cancel", nk::DialogResponse::Cancel);
        dlg->add_button("OK", nk::DialogResponse::Accept);
        (void)dlg->on_response().connect(
            [&status_bar](nk::DialogResponse const& resp) {
                if (resp == nk::DialogResponse::Accept) {
                    status_bar->set_segment(0, "Dialog: Accepted");
                } else {
                    status_bar->set_segment(0, "Dialog: Cancelled");
                }
            });
        dlg->present(window);
    });

    auto actions_content = Box::vertical(12.0F);
    actions_content->append(actions_title);
    actions_content->append(actions_subtitle);
    actions_content->append(prop_label);
    actions_content->append(prop_value_label);
    actions_content->append(prop_btn);
    actions_content->append(dialog_label);
    actions_content->append(dialog_btn);
    auto actions_card = SurfacePanel::card(actions_content);

    auto left_column = Box::vertical(18.0F);
    left_column->append(controls_card);
    left_column->append(list_card);

    auto right_column = Box::vertical(18.0F);
    right_column->append(preview_card);
    right_column->append(actions_card);

    auto content_row = SplitColumns::create(left_column, right_column, 0.52F, 22.0F);

    auto hero_content = Box::vertical(4.0F);
    auto hero_title = nk::Label::create("NodalKit Linux Showcase");
    hero_title->add_style_class("heading");
    auto hero_subtitle = nk::Label::create(
        "A calmer Linux-first pass with clearer hierarchy and working controls.");
    hero_subtitle->add_style_class("muted");
    hero_content->append(hero_title);
    hero_content->append(hero_subtitle);
    auto hero_card = SurfacePanel::card(hero_content);

    auto body_content = Box::vertical(20.0F);
    body_content->append(hero_card);
    body_content->append(content_row);

    auto body_page = SurfacePanel::page(body_content);
    auto root = ShowcaseShell::create(menu_bar, body_page, status_bar);
    window.set_child(root);

    // -----------------------------------------------------------------------
    // Menu actions.
    // -----------------------------------------------------------------------
    (void)menu_bar->on_action().connect(
        [&app, &status_bar, &window](std::string_view const& action) {
            if (action == "file.quit") {
                app.quit(0);
            } else if (action == "help.about") {
                auto dlg = nk::Dialog::create(
                    "About", "NodalKit Showcase v0.1.0\n"
                             "A C++23 GUI Toolkit");
                dlg->add_button("OK", nk::DialogResponse::Accept);
                dlg->present(window);
            } else {
                status_bar->set_segment(
                    0, "Action: " + std::string(action));
            }
        });

    // -----------------------------------------------------------------------
    // Animate the image view (update every ~33ms = ~30fps).
    // -----------------------------------------------------------------------
    (void)app.event_loop().set_interval(
        std::chrono::milliseconds(33),
        [&image_view, &frame_number] {
            ++frame_number;
            auto px = generate_test_pattern(kImgW, kImgH, frame_number);
            image_view->update_pixel_buffer(px.data(), kImgW, kImgH);
        });

    // -----------------------------------------------------------------------
    // Window close handler.
    // -----------------------------------------------------------------------
    (void)window.on_close_request().connect([&app] {
        app.quit(0);
    });

    window.present();
    return app.run();
}
