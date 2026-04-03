#pragma once

/// @file macos_window.h
/// @brief macOS Cocoa native surface (private header).

#include <cstdint>
#include <memory>
#include <nk/platform/platform_backend.h>
#include <nk/platform/window.h>
#include <string_view>
#include <vector>

#ifdef __OBJC__
@class NSWindow;
@class NKView;
@class NKWindowDelegate;
#else
using NSWindow = void;
using NKView = void;
using NKWindowDelegate = void;
#endif

namespace nk {

class MacosSurface : public NativeSurface {
public:
    MacosSurface(const WindowConfig& config, Window& owner);
    ~MacosSurface() override;

    void show() override;
    void hide() override;
    void set_title(std::string_view title) override;
    void resize(int width, int height) override;
    [[nodiscard]] Size size() const override;
    [[nodiscard]] float scale_factor() const override;
    [[nodiscard]] RendererBackendSupport renderer_backend_support() const override;
    void present(const uint8_t* rgba, int w, int h, std::span<const Rect> damage_regions) override;
    void set_fullscreen(bool fullscreen) override;
    [[nodiscard]] bool is_fullscreen() const override;
    [[nodiscard]] NativeWindowHandle native_handle() const override;
    [[nodiscard]] NativeWindowHandle native_display_handle() const override;
    void set_cursor_shape(CursorShape shape) override;

    /// Access the owning Window for event delivery from Objective-C code.
    Window& owner() { return owner_; }

    /// Access the pixel buffer for drawRect painting.
    const uint8_t* pixel_data() const { return pixel_buffer_.data(); }

    int pixel_width() const { return pixel_width_; }

    int pixel_height() const { return pixel_height_; }

    bool has_pixels() const { return !pixel_buffer_.empty(); }

private:
    Window& owner_;
    NSWindow* window_ = nullptr;
    NKView* view_ = nullptr;
    NKWindowDelegate* window_delegate_ = nullptr;

    std::vector<uint8_t> pixel_buffer_;
    int pixel_width_ = 0;
    int pixel_height_ = 0;
    bool fullscreen_ = false;
};

} // namespace nk
