#include <nk/widgets/list_view.h>

#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>

#include <algorithm>
#include <cmath>

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
    float row_height = 24.0F;
    float scroll_offset = 0.0F;
    Signal<std::size_t> row_activated;
    ScopedConnection selection_changed;
    ScopedConnection current_changed;
};

Rect list_body_rect(ListView const& view) {
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

Rect list_inner_rect(ListView const& view) {
    auto const body = list_body_rect(view);
    return {
        body.x + 1.0F,
        body.y + 1.0F,
        std::max(0.0F, body.width - 2.0F),
        std::max(0.0F, body.height - 2.0F),
    };
}

float max_scroll_offset(
    AbstractListModel const* model,
    float row_height,
    float viewport_height) {
    if (!model || viewport_height <= 0.0F) {
        return 0.0F;
    }
    float const content_height =
        static_cast<float>(model->row_count()) * row_height;
    return std::max(0.0F, content_height - viewport_height);
}

void clamp_scroll_offset(
    float& scroll_offset,
    AbstractListModel const* model,
    float row_height,
    float viewport_height) {
    scroll_offset = std::clamp(
        scroll_offset,
        0.0F,
        max_scroll_offset(model, row_height, viewport_height));
}

std::size_t invalid_row() {
    return static_cast<std::size_t>(-1);
}

std::size_t current_row_index(SelectionModel const* selection) {
    if (!selection) {
        return invalid_row();
    }
    auto const current = selection->current_row();
    if (current != invalid_row()) {
        return current;
    }
    auto const& selected = selection->selected_rows();
    if (!selected.empty()) {
        return *selected.begin();
    }
    return invalid_row();
}

int row_at_point(
    AbstractListModel const* model,
    float row_height,
    float scroll_offset,
    Rect viewport,
    Point point) {
    if (!model || !viewport.contains(point)) {
        return -1;
    }
    float const local_y = point.y - viewport.y + scroll_offset;
    int const row = static_cast<int>(local_y / row_height);
    if (row < 0 || row >= static_cast<int>(model->row_count())) {
        return -1;
    }
    return row;
}

std::shared_ptr<ListView> ListView::create() {
    return std::shared_ptr<ListView>(new ListView());
}

ListView::ListView()
    : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("list-view");
}

ListView::~ListView() = default;

void ListView::set_model(std::shared_ptr<AbstractListModel> model) {
    impl_->model = std::move(model);
    clamp_scroll_offset(
        impl_->scroll_offset,
        impl_->model.get(),
        impl_->row_height,
        list_inner_rect(*this).height);
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
            impl_->selection->on_selection_changed().connect([this] {
                queue_redraw();
            }));
        impl_->current_changed = ScopedConnection(
            impl_->selection->on_current_changed().connect([this](std::size_t) {
                queue_redraw();
            }));
    }
    queue_redraw();
}

SelectionModel* ListView::selection_model() const {
    return impl_->selection.get();
}

void ListView::set_item_factory(ItemFactory factory) {
    impl_->factory = std::move(factory);
    queue_layout();
}

void ListView::set_row_height(float height) {
    impl_->row_height = height;
    clamp_scroll_offset(
        impl_->scroll_offset,
        impl_->model.get(),
        impl_->row_height,
        list_inner_rect(*this).height);
    queue_layout();
}

Signal<std::size_t>& ListView::on_row_activated() {
    return impl_->row_activated;
}

SizeRequest ListView::measure(Constraints const& /*constraints*/) const {
    float h = impl_->row_height * 6.0F;
    if (impl_->model) {
        auto const viewport_rows =
            std::min<std::size_t>(8, std::max<std::size_t>(4, impl_->model->row_count()));
        h = static_cast<float>(viewport_rows) * impl_->row_height;
    }
    return {160.0F, impl_->row_height * 4.0F, 300.0F, h + 2.0F};
}

void ListView::allocate(Rect const& allocation) {
    Widget::allocate(allocation);
    clamp_scroll_offset(
        impl_->scroll_offset,
        impl_->model.get(),
        impl_->row_height,
        list_inner_rect(*this).height);
    // Stub: a real implementation would create/recycle item widgets
    // for visible rows using the factory and model.
}

bool ListView::handle_mouse_event(MouseEvent const& event) {
    auto const inner = list_inner_rect(*this);

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (event.button == 1) {
            int const row = row_at_point(
                impl_->model.get(),
                impl_->row_height,
                impl_->scroll_offset,
                inner,
                {event.x, event.y});
            if (row >= 0 && impl_->selection) {
                auto const row_index = static_cast<std::size_t>(row);
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
        impl_->scroll_offset = std::clamp(
            impl_->scroll_offset - (event.scroll_dy * impl_->row_height),
            0.0F,
            max_scroll_offset(impl_->model.get(), impl_->row_height, inner.height));
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

bool ListView::handle_key_event(KeyEvent const& event) {
    if (event.type != KeyEvent::Type::Press || !impl_->model
        || impl_->model->row_count() == 0) {
        return false;
    }

    auto select_row = [this](std::size_t row) {
        if (!impl_->selection) {
            return;
        }
        impl_->selection->set_current_row(row);
        impl_->selection->select(row);

        auto const inner = list_inner_rect(*this);
        float const row_top = static_cast<float>(row) * impl_->row_height;
        float const row_bottom = row_top + impl_->row_height;
        if (row_top < impl_->scroll_offset) {
            impl_->scroll_offset = row_top;
        } else if (row_bottom > impl_->scroll_offset + inner.height) {
            impl_->scroll_offset = row_bottom - inner.height;
        }
        clamp_scroll_offset(
            impl_->scroll_offset,
            impl_->model.get(),
            impl_->row_height,
            inner.height);
        queue_redraw();
    };

    auto const row_count = impl_->model->row_count();
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
        auto const inner = list_inner_rect(*this);
        auto const page_rows = std::max<std::size_t>(
            1, static_cast<std::size_t>(inner.height / impl_->row_height));
        select_row(current > page_rows ? current - page_rows : 0);
        return true;
    }
    case KeyCode::PageDown: {
        auto const inner = list_inner_rect(*this);
        auto const page_rows = std::max<std::size_t>(
            1, static_cast<std::size_t>(inner.height / impl_->row_height));
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
    auto const a = allocation();
    float const corner_radius = theme_number("corner-radius", 12.0F);
    float const selection_radius = theme_number("selection-radius", 8.0F);
    auto body = a;
    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(
            a,
            theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
            corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    ctx.add_rounded_rect(
        body,
        theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}),
        corner_radius);
    ctx.add_border(
        body,
        theme_color("border-color", Color{0.86F, 0.88F, 0.91F, 1.0F}),
        1.0F,
        corner_radius);
    Rect inner = {body.x + 1.0F, body.y + 1.0F,
                  std::max(0.0F, body.width - 2.0F),
                  std::max(0.0F, body.height - 2.0F)};

    if (impl_->model) {
        auto const total_rows = impl_->model->row_count();
        auto const row_h = impl_->row_height;
        auto const font = list_font();
        float const total_height = static_cast<float>(total_rows) * row_h;
        bool const show_scrollbar = total_height > inner.height;
        float const scrollbar_width = show_scrollbar ? 10.0F : 0.0F;
        Rect content_rect = {
            inner.x,
            inner.y,
            std::max(0.0F, inner.width - scrollbar_width - (show_scrollbar ? 6.0F : 0.0F)),
            inner.height,
        };
        auto const text_color =
            theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
        auto const selected_bg =
            theme_color("selected-background", Color{0.86F, 0.92F, 0.99F, 1.0F});
        auto const selected_text =
            theme_color("selected-text-color", text_color);
        auto const separator =
            theme_color("row-separator-color", Color{0.9F, 0.91F, 0.94F, 1.0F});
        auto const scrollbar_track = Color{0.16F, 0.19F, 0.23F, 0.06F};
        auto const scrollbar_thumb = Color{0.16F, 0.19F, 0.23F, 0.22F};

        auto const first_row =
            static_cast<std::size_t>(impl_->scroll_offset / row_h);
        float y = content_rect.y - std::fmod(impl_->scroll_offset, row_h);

        for (std::size_t i = first_row; i < total_rows && y < content_rect.bottom(); ++i) {
            if (y + row_h <= content_rect.y) {
                y += row_h;
                continue;
            }

            auto text = impl_->model->display_text(i);
            bool selected = impl_->selection && impl_->selection->is_selected(i);
            auto const measured = measure_text(text, font);
            float const text_y =
                y + std::max(0.0F, (row_h - measured.height) * 0.5F);
            if (selected) {
                ctx.add_rounded_rect(
                    {content_rect.x + 4.0F, y + 2.0F,
                     content_rect.width - 8.0F, row_h - 4.0F},
                    selected_bg,
                    selection_radius);
                ctx.add_text(
                    {content_rect.x + 12.0F, text_y},
                    text,
                    selected_text,
                    font);
            } else {
                ctx.add_text(
                    {content_rect.x + 12.0F, text_y},
                    text,
                    text_color,
                    font);
            }
            ctx.add_color_rect(
                {content_rect.x, y + row_h - 1.0F, content_rect.width, 1.0F},
                separator);
            y += row_h;
        }

        if (show_scrollbar) {
            float const track_x = inner.right() - scrollbar_width;
            ctx.add_rounded_rect(
                {track_x, inner.y + 4.0F, scrollbar_width, inner.height - 8.0F},
                scrollbar_track,
                scrollbar_width * 0.5F);

            float const max_offset =
                std::max(1.0F, total_height - content_rect.height);
            float const thumb_height = std::max(
                28.0F,
                (content_rect.height / total_height) * (inner.height - 8.0F));
            float const thumb_y = inner.y + 4.0F
                + (impl_->scroll_offset / max_offset)
                    * ((inner.height - 8.0F) - thumb_height);
            ctx.add_rounded_rect(
                {track_x + 2.0F, thumb_y, scrollbar_width - 4.0F, thumb_height},
                scrollbar_thumb,
                (scrollbar_width - 4.0F) * 0.5F);
        }
    }
}

} // namespace nk
