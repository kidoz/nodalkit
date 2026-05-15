#include <nk/render/snapshot_context.h>
#include <nk/widgets/canvas_widget.h>

namespace nk {

struct CanvasWidget::Impl {
    Signal<SnapshotContext&, Rect> draw;
};

std::shared_ptr<CanvasWidget> CanvasWidget::create() {
    return std::shared_ptr<CanvasWidget>(new CanvasWidget());
}

CanvasWidget::CanvasWidget() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("canvas");
}

CanvasWidget::~CanvasWidget() = default;

Signal<SnapshotContext&, Rect>& CanvasWidget::on_draw() {
    return impl_->draw;
}

void CanvasWidget::queue_canvas_redraw() {
    queue_redraw();
}

SizeRequest CanvasWidget::measure(const Constraints& constraints) const {
    return {0.0F, 0.0F, constraints.max_width, constraints.max_height};
}

void CanvasWidget::snapshot(SnapshotContext& ctx) const {
    const auto rect = allocation();
    // In a fully hardware-accelerated pipeline, we would push an off-screen 
    // rendering node here. For now, we group the drawing commands in a container.
    ctx.push_container(rect);
    impl_->draw.emit(ctx, rect);
    ctx.pop_container();
}

} // namespace nk