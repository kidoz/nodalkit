#pragma once

/// @file clamp.h
/// @brief Single-child container that limits readable content width or height.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// Centers one child and smoothly limits its size along one axis.
class Clamp : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Clamp>
    create(Orientation orientation = Orientation::Horizontal);
    ~Clamp() override;

    void set_child(std::shared_ptr<Widget> child);
    [[nodiscard]] Widget* child() const;

    [[nodiscard]] Orientation orientation() const;

    [[nodiscard]] float maximum_size() const;
    void set_maximum_size(float size);

    [[nodiscard]] float tightening_threshold() const;
    void set_tightening_threshold(float size);

    [[nodiscard]] bool scales_with_text() const;
    void set_scales_with_text(bool enabled);

    [[nodiscard]] bool has_height_for_width() const override;
    [[nodiscard]] float height_for_width(float width) const override;
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    explicit Clamp(Orientation orientation);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
