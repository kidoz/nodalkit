#pragma once

/// @file avatar.h
/// @brief Circular user/entity image or initials display.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// Presence shown on the bottom-trailing edge of an avatar.
enum class AvatarStatus {
    None,
    Online,
    Busy,
    Away,
    Offline,
};

/// A circular avatar showing an image, initials, or a person mark.
///
/// When both the initials and the image are empty, the circle draws a person
/// mark. The accessible name stays "Avatar". `set_status()` draws a small
/// mark at the bottom-trailing edge. `None` draws nothing. The accessible
/// description names the status when one is set.
class Avatar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Avatar> create(std::string initials = {});
    ~Avatar() override;

    [[nodiscard]] std::string_view initials() const;
    void set_initials(std::string initials);

    /// Set a pixel buffer to display. ARGB8888 row-major.
    void set_image(const uint32_t* data, int width, int height);
    void clear_image();

    void set_status(AvatarStatus status);
    [[nodiscard]] AvatarStatus status() const;

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
