#pragma once

/// @file avatar.h
/// @brief Circular user/entity image or initials display.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A circular avatar showing an image or fallback initials.
class Avatar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Avatar> create(std::string initials = {});
    ~Avatar() override;

    [[nodiscard]] std::string_view initials() const;
    void set_initials(std::string initials);

    /// Set a pixel buffer to display. ARGB8888 row-major.
    void set_image(const uint32_t* data, int width, int height);
    void clear_image();

    /// Diameter in logical pixels.
    [[nodiscard]] float diameter() const;
    void set_diameter(float diameter);

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    explicit Avatar(std::string initials);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
