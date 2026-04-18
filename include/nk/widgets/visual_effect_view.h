#pragma once

/// @file visual_effect_view.h
/// @brief Backdrop material container for sidebars, popovers, and other
/// surfaces that want the native macOS vibrancy look.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// Standard backdrop materials. On macOS these map to NSVisualEffectMaterial
/// values; on other platforms (and until the macOS native wiring lands in a
/// follow-up slice) they select a themed translucent fallback color.
enum class VisualEffectMaterial {
    Sidebar,
    HeaderView,
    WindowBackground,
    Popover,
    Menu,
    Tooltip,
};

/// Single-child container that paints a translucent backdrop material behind
/// its child.
///
/// The cross-platform fallback draws a tinted translucent rectangle (optionally
/// rounded) so applications can use this widget unconditionally; slice 2 adds
/// real NSVisualEffectView compositing on macOS once the window surface grows a
/// non-opaque presentation path.
class VisualEffectView : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<VisualEffectView> create(
        VisualEffectMaterial material = VisualEffectMaterial::Sidebar);
    ~VisualEffectView() override;

    void set_child(std::shared_ptr<Widget> child);
    [[nodiscard]] Widget* child() const;

    [[nodiscard]] VisualEffectMaterial material() const;
    void set_material(VisualEffectMaterial material);

    /// Corner radius applied to the backdrop fallback. Native macOS vibrancy
    /// will follow the containing window's corner shape and ignore this value.
    [[nodiscard]] float corner_radius() const;
    void set_corner_radius(float radius);

    /// Override the fallback tint. When unset the tint follows the theme token
    /// associated with the current material.
    void set_fallback_tint(Color color);
    void clear_fallback_tint();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    explicit VisualEffectView(VisualEffectMaterial material);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
