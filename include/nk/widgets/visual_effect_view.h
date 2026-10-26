#pragma once

/// @file visual_effect_view.h
/// @brief Themed backdrop container for sidebars, popovers, and other surfaces.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// Standard backdrop materials that select a themed surface color on every
/// platform. This widget does not use native NSVisualEffectView compositing.
enum class VisualEffectMaterial {
    Sidebar,
    HeaderView,
    WindowBackground,
    Popover,
    Menu,
    Tooltip,
};

/// Single-child container that paints a themed backdrop material behind
/// its child.
///
/// Each material resolves a semantic surface token through the theme's shared
/// rules (sidebar, headerbar, popover, ...), so the backdrop follows the
/// active color scheme; the literal tints are only defensive fallbacks for
/// themeless use. The material's color determines the backdrop opacity.
class VisualEffectView : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<VisualEffectView>
    create(VisualEffectMaterial material = VisualEffectMaterial::Sidebar);
    ~VisualEffectView() override;

    void set_child(std::shared_ptr<Widget> child);
    [[nodiscard]] Widget* child() const;

    [[nodiscard]] VisualEffectMaterial material() const;
    void set_material(VisualEffectMaterial material);

    /// Corner radius applied to the painted backdrop.
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
