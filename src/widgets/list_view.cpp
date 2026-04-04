#include <algorithm>
#include <cmath>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/list_view.h>

namespace nk {

namespace {

FontDescriptor list_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

} // namespace

struct ListView::Impl {
    std::shared_ptr<AbstractListModel> model;
    std::shared_ptr<SelectionModel> selection;
    ItemFactory factory;
    std::vector<std::pair<std::size_t, std::shared_ptr<Widget>>> visible_items;
    float row_height = 24.0F;
    float scroll_offset = 0.0F;
    Signal<std::size_t> row_activated;
    ScopedConnection rows_inserted;
    ScopedConnection rows_removed;
    ScopedConnection data_changed;
    ScopedConnection model_reset;
    ScopedConnection selection_changed;
    ScopedConnection current_changed;
};

Rect list_body_rect(const ListView& view) {
    auto body = view.allocation();
    if (has_flag(view.state_flags(), StateFlags::Focused)) {
        body = {
            body.x + 2.0F,
            body.y + 2.0F,
            body.width - 4.0F,
            body.height - 4.0F,
        };
    }
    return body;
}

Rect list_inner_rect(const ListView& view) {
    const auto body = list_body_rect(view);
    return {
        body.x + 1.0F,
        body.y + 1.0F,
        std::max(0.0F, body.width - 2.0F),
        std::max(0.0F, body.height - 2.0F),
    };
}

float max_scroll_offset(const AbstractListModel* model, float row_height, float viewport_height) {
    if (!model || viewport_height <= 0.0F) {
        return 0.0F;
    }
    const float content_height = static_cast<float>(model->row_count()) * row_height;
    return std::max(0.0F, content_height - viewport_height);
}

void clamp_scroll_offset(float& scroll_offset,
                         const AbstractListModel* model,
                         float row_height,
                         float viewport_height) {
    scroll_offset =
        std::clamp(scroll_offset, 0.0F, max_scroll_offset(model, row_height, viewport_height));
}

std::size_t invalid_row() {
    return static_cast<std::size_t>(-1);
}

std::size_t current_row_index(const SelectionModel* selection) {
    if (!selection) {
        return invalid_row();
    }
    const auto current = selection->current_row();
    if (current != invalid_row()) {
        return current;
    }
    const auto& selected = selection->selected_rows();
    if (!selected.empty()) {
        return *selected.begin();
    }
    return invalid_row();
}

int row_at_point(const AbstractListModel* model,
                 float row_height,
                 float scroll_offset,
                 Rect viewport,
                 Point point) {
    if (!model || !viewport.contains(point)) {
        return -1;
    }
    const float local_y = point.y - viewport.y + scroll_offset;
    const int row = static_cast<int>(local_y / row_height);
    if (row < 0 || row >= static_cast<int>(model->row_count())) {
        return -1;
    }
    return row;
}

std::shared_ptr<ListView> ListView::create() {
    return std::shared_ptr<ListView>(new ListView());
}

ListView::ListView() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("list-view");
}

ListView::~ListView() = default;

void ListView::set_model(std::shared_ptr<AbstractListModel> model) {
    (void)clear_visible_items();
    impl_->model = std::move(model);
    impl_->rows_inserted.disconnect();
    impl_->rows_removed.disconnect();
    impl_->data_changed.disconnect();
    impl_->model_reset.disconnect();
    if (impl_->model) {
        auto refresh = [this] {
            (void)clear_visible_items();
            queue_layout();
            queue_redraw();
        };
        impl_->rows_inserted = ScopedConnection(impl_->model->on_rows_inserted().connect(
            [refresh](std::size_t, std::size_t) { refresh(); }));
        impl_->rows_removed = ScopedConnection(impl_->model->on_rows_removed().connect(
            [refresh](std::size_t, std::size_t) { refresh(); }));
        impl_->data_changed = ScopedConnection(impl_->model->on_data_changed().connect(
            [refresh](std::size_t, std::size_t) { refresh(); }));
        impl_->model_reset =
            ScopedConnection(impl_->model->on_model_reset().connect([refresh] { refresh(); }));
    }
    clamp_scroll_offset(
        impl_->scroll_offset, impl_->model.get(), impl_->row_height, list_inner_rect(*this).height);
    queue_layout();
    queue_redraw();
}

AbstractListModel* ListView::model() const {
    return impl_->model.get();
}

void ListView::set_selection_model(std::shared_ptr<SelectionModel> model) {
    impl_->selection = std::move(model);
    impl_->selection_changed.disconnect();
    impl_->current_changed.disconnect();
    if (impl_->selection) {
        impl_->selection_changed = ScopedConnection(
            impl_->selection->on_selection_changed().connect([this] { queue_redraw(); }));
        impl_->current_changed = ScopedConnection(impl_->selection->on_current_changed().connect(
            [this](std::size_t) { queue_redraw(); }));
    }
    queue_redraw();
}

SelectionModel* ListView::selection_model() const {
    return impl_->selection.get();
}

void ListView::set_item_factory(ItemFactory factory) {
    impl_->factory = std::move(factory);
    (void)clear_visible_items();
    queue_layout();
    queue_redraw();
}

void ListView::set_row_height(float height) {
    impl_->row_height = height;
    clamp_scroll_offset(
        impl_->scroll_offset, impl_->model.get(), impl_->row_height, list_inner_rect(*this).height);
    queue_layout();
}

Signal<std::size_t>& ListView::on_row_activated() {
    return impl_->row_activated;
}

SizeRequest ListView::measure(const Constraints& /*constraints*/) const {
    float h = impl_->row_height * 6.0F;
    if (impl_->model) {
        const auto viewport_rows =
            std::min<std::size_t>(8, std::max<std::size_t>(4, impl_->model->row_count()));
        h = static_cast<float>(viewport_rows) * impl_->row_height;
    }
    return {160.0F, impl_->row_height * 4.0F, 300.0F, h + 2.0F};
}

void ListView::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    clamp_scroll_offset(
        impl_->scroll_offset, impl_->model.get(), impl_->row_height, list_inner_rect(*this).height);
    sync_visible_items();
}

bool ListView::handle_mouse_event(const MouseEvent& event) {
    const auto inner = list_inner_rect(*this);

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (event.button == 1) {
            const int row = row_at_point(impl_->model.get(),
                                         impl_->row_height,
                                         impl_->scroll_offset,
                                         inner,
                                         {event.x, event.y});
            if (row >= 0 && impl_->selection) {
                const auto row_index = static_cast<std::size_t>(row);
                impl_->selection->set_current_row(row_index);
                impl_->selection->select(row_index);
                queue_redraw();
                return true;
            }
        }
        return inner.contains({event.x, event.y});
    case MouseEvent::Type::Scroll:
        if (!inner.contains({event.x, event.y}) || !impl_->model) {
            return false;
        }
        impl_->scroll_offset =
            std::clamp(impl_->scroll_offset - (event.scroll_dy * impl_->row_height),
                       0.0F,
                       max_scroll_offset(impl_->model.get(), impl_->row_height, inner.height));
        sync_visible_items();
        queue_redraw();
        return true;
    case MouseEvent::Type::Release:
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
        return false;
    }

    return false;
}

bool ListView::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press || !impl_->model || impl_->model->row_count() == 0) {
        return false;
    }

    auto select_row = [this](std::size_t row) {
        if (!impl_->selection) {
            return;
        }
        impl_->selection->set_current_row(row);
        impl_->selection->select(row);

        const auto inner = list_inner_rect(*this);
        const float row_top = static_cast<float>(row) * impl_->row_height;
        const float row_bottom = row_top + impl_->row_height;
        if (row_top < impl_->scroll_offset) {
            impl_->scroll_offset = row_top;
        } else if (row_bottom > impl_->scroll_offset + inner.height) {
            impl_->scroll_offset = row_bottom - inner.height;
        }
        clamp_scroll_offset(
            impl_->scroll_offset, impl_->model.get(), impl_->row_height, inner.height);
        sync_visible_items();
        queue_redraw();
    };

    const auto row_count = impl_->model->row_count();
    std::size_t current = current_row_index(impl_->selection.get());
    if (current == invalid_row()) {
        current = 0;
    }

    switch (event.key) {
    case KeyCode::Up:
        select_row(current > 0 ? current - 1 : 0);
        return true;
    case KeyCode::Down:
        select_row(std::min(current + 1, row_count - 1));
        return true;
    case KeyCode::Home:
        select_row(0);
        return true;
    case KeyCode::End:
        select_row(row_count - 1);
        return true;
    case KeyCode::PageUp: {
        const auto inner = list_inner_rect(*this);
        const auto page_rows =
            std::max<std::size_t>(1, static_cast<std::size_t>(inner.height / impl_->row_height));
        select_row(current > page_rows ? current - page_rows : 0);
        return true;
    }
    case KeyCode::PageDown: {
        const auto inner = list_inner_rect(*this);
        const auto page_rows =
            std::max<std::size_t>(1, static_cast<std::size_t>(inner.height / impl_->row_height));
        select_row(std::min(current + page_rows, row_count - 1));
        return true;
    }
    case KeyCode::Return:
    case KeyCode::Space:
        impl_->row_activated.emit(current);
        return true;
    default:
        return false;
    }
}

void ListView::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 12.0F);
    const float selection_radius = theme_number("selection-radius", 8.0F);
    auto body = a;
    if (has_flag(state_flags(), StateFlags::Focused)) {
        const auto focus_ring = theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F});
        ctx.add_rounded_rect(
            a, Color{focus_ring.r, focus_ring.g, focus_ring.b, 0.08F}, corner_radius + 1.5F);
        body = {a.x + 1.5F, a.y + 1.5F, a.width - 3.0F, a.height - 3.0F};
    }

    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.86F, 0.88F, 0.91F, 1.0F}), 1.0F, corner_radius);
    Rect inner = {body.x + 1.0F,
                  body.y + 1.0F,
                  std::max(0.0F, body.width - 2.0F),
                  std::max(0.0F, body.height - 2.0F)};

    if (impl_->model) {
        const auto total_rows = impl_->model->row_count();
        const auto row_h = impl_->row_height;
        const auto font = list_font();
        const float total_height = static_cast<float>(total_rows) * row_h;
        const bool show_scrollbar = total_height > inner.height;
        const float scrollbar_width = show_scrollbar ? 11.0F : 0.0F;
        Rect content_rect = {
            inner.x,
            inner.y,
            std::max(0.0F, inner.width - scrollbar_width - (show_scrollbar ? 12.0F : 0.0F)),
            inner.height,
        };
        const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
        const auto selected_bg =
            theme_color("selected-background", Color{0.86F, 0.92F, 0.99F, 1.0F});
        const auto selected_text = theme_color("selected-text-color", text_color);
        const auto separator = theme_color("row-separator-color", Color{0.9F, 0.91F, 0.94F, 1.0F});
        const auto scrollbar_track =
            theme_color("scrollbar-track-color", Color{0.88F, 0.90F, 0.93F, 1.0F});
        const auto scrollbar_thumb =
            theme_color("scrollbar-thumb-color", Color{0.67F, 0.71F, 0.76F, 1.0F});

        const auto first_row = static_cast<std::size_t>(impl_->scroll_offset / row_h);
        float y = content_rect.y - std::fmod(impl_->scroll_offset, row_h);

        for (std::size_t i = first_row; i < total_rows && y < content_rect.bottom(); ++i) {
            if (y + row_h <= content_rect.y) {
                y += row_h;
                continue;
            }

            const bool selected = impl_->selection && impl_->selection->is_selected(i);
            if (selected) {
                ctx.add_rounded_rect(
                    {content_rect.x + 4.0F, y + 2.0F, content_rect.width - 8.0F, row_h - 4.0F},
                    Color{selected_bg.r, selected_bg.g, selected_bg.b, 0.82F},
                    selection_radius);
            }

            if (!impl_->factory) {
                auto text = impl_->model->display_text(i);
                const auto measured = measure_text(text, font);
                const float text_y = y + std::max(0.0F, (row_h - measured.height) * 0.5F);
                ctx.add_text({content_rect.x + 12.0F, text_y},
                             text,
                             selected ? selected_text : text_color,
                             font);
            }

            ctx.add_color_rect({content_rect.x, y + row_h - 1.0F, content_rect.width, 1.0F},
                               separator);
            y += row_h;
        }

        if (impl_->factory && !impl_->visible_items.empty()) {
            ctx.push_rounded_clip(content_rect, selection_radius);
            Widget::snapshot(ctx);
            ctx.pop_container();
        }

        if (show_scrollbar) {
            const float track_x = inner.right() - scrollbar_width - 4.0F;
            ctx.add_rounded_rect({track_x, inner.y + 6.0F, scrollbar_width, inner.height - 12.0F},
                                 scrollbar_track,
                                 scrollbar_width * 0.5F);

            const float max_offset = std::max(1.0F, total_height - content_rect.height);
            const float thumb_height =
                std::max(28.0F, (content_rect.height / total_height) * (inner.height - 12.0F));
            const float thumb_y =
                inner.y + 6.0F +
                (impl_->scroll_offset / max_offset) * ((inner.height - 12.0F) - thumb_height);
            ctx.add_rounded_rect({track_x + 2.0F, thumb_y, scrollbar_width - 4.0F, thumb_height},
                                 scrollbar_thumb,
                                 (scrollbar_width - 4.0F) * 0.5F);
        }
    }
}

std::size_t ListView::clear_visible_items() {
    std::size_t disposed = 0;
    for (auto& [row, widget] : impl_->visible_items) {
        (void)row;
        if (widget) {
            remove_child(*widget);
            ++disposed;
        }
    }
    impl_->visible_items.clear();
    return disposed;
}

void ListView::sync_visible_items() {
    if (!impl_->factory || !impl_->model) {
        note_model_view_sync_for_diagnostics(0, 0, clear_visible_items());
        return;
    }

    const auto inner = list_inner_rect(*this);
    if (inner.width <= 0.0F || inner.height <= 0.0F || impl_->row_height <= 0.0F ||
        impl_->model->row_count() == 0) {
        note_model_view_sync_for_diagnostics(0, 0, clear_visible_items());
        return;
    }

    const auto total_rows = impl_->model->row_count();
    const float total_height = static_cast<float>(total_rows) * impl_->row_height;
    const bool show_scrollbar = total_height > inner.height;
    const float scrollbar_width = show_scrollbar ? 10.0F : 0.0F;
    Rect content_rect = {
        inner.x,
        inner.y,
        std::max(0.0F, inner.width - scrollbar_width - (show_scrollbar ? 6.0F : 0.0F)),
        inner.height};

    const auto first_row = static_cast<std::size_t>(impl_->scroll_offset / impl_->row_height);
    const auto last_row = std::min<std::size_t>(
        total_rows - 1,
        static_cast<std::size_t>(std::max(
            0.0F, (impl_->scroll_offset + content_rect.height - 1.0F) / impl_->row_height)));

    std::vector<std::pair<std::size_t, std::shared_ptr<Widget>>> next_visible_items;
    next_visible_items.reserve(last_row - first_row + 1);
    std::size_t materialized_rows = 0;
    std::size_t reused_rows = 0;
    std::size_t disposed_rows = 0;

    for (std::size_t row = first_row; row <= last_row; ++row) {
        std::shared_ptr<Widget> row_widget;
        auto existing = std::find_if(impl_->visible_items.begin(),
                                     impl_->visible_items.end(),
                                     [row](const auto& entry) { return entry.first == row; });
        if (existing != impl_->visible_items.end()) {
            row_widget = existing->second;
            if (row_widget) {
                ++reused_rows;
            }
        } else {
            row_widget = impl_->factory(row);
            if (row_widget) {
                append_child(row_widget);
                ++materialized_rows;
            }
        }

        if (!row_widget) {
            continue;
        }

        const float row_top = content_rect.y - std::fmod(impl_->scroll_offset, impl_->row_height) +
                              static_cast<float>(row - first_row) * impl_->row_height;
        row_widget->allocate({content_rect.x + 12.0F,
                              row_top + 3.0F,
                              std::max(0.0F, content_rect.width - 24.0F),
                              std::max(0.0F, impl_->row_height - 6.0F)});
        next_visible_items.push_back({row, row_widget});
    }

    for (auto& [row, widget] : impl_->visible_items) {
        auto still_visible = std::find_if(next_visible_items.begin(),
                                          next_visible_items.end(),
                                          [row](const auto& entry) { return entry.first == row; });
        if (still_visible == next_visible_items.end() && widget) {
            remove_child(*widget);
            ++disposed_rows;
        }
    }

    impl_->visible_items = std::move(next_visible_items);
    note_model_view_sync_for_diagnostics(materialized_rows, reused_rows, disposed_rows);
}

} // namespace nk
