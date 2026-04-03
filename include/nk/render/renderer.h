#pragma once

/// @file renderer.h
/// @brief Abstract renderer backend.

#include <memory>
#include <nk/debug/diagnostics.h>
#include <nk/foundation/types.h>
#include <optional>
#include <span>
#include <string_view>

namespace nk {

class RenderNode;
class NativeSurface;
class TextShaper;

enum class RendererBackend {
    Software,
    Metal,
    OpenGL,
    Vulkan,
};

struct RendererBackendSupport {
    bool software = true;
    bool metal = false;
    bool open_gl = false;
    bool vulkan = false;
};

[[nodiscard]] std::string_view renderer_backend_name(RendererBackend backend) noexcept;
[[nodiscard]] bool renderer_backend_is_gpu(RendererBackend backend) noexcept;
[[nodiscard]] bool renderer_backend_supported(RendererBackendSupport support,
                                              RendererBackend backend) noexcept;
[[nodiscard]] bool renderer_backend_available(RendererBackend backend) noexcept;

/// Abstract renderer backend. The MVP ships a software renderer that
/// rasterizes render nodes to a pixel buffer. Future backends will
/// target Vulkan, OpenGL, or Metal.
class Renderer {
public:
    virtual ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] virtual RendererBackend backend() const = 0;

    /// Attach the renderer to a platform surface if it needs native presentation state.
    virtual bool attach_surface(NativeSurface& surface);

    /// Set the text shaper used for text rendering.
    virtual void set_text_shaper(TextShaper* shaper);

    /// Provide optional damage regions for the current frame. Backends may
    /// ignore this and redraw the full scene.
    virtual void set_damage_regions(std::span<const Rect> regions);

    /// Begin a frame for the given logical viewport and device scale.
    virtual void begin_frame(Size viewport, float scale_factor) = 0;

    /// Render the given render-node tree.
    virtual void render(const RenderNode& root) = 0;

    /// End the frame and finalize renderer-side work.
    virtual void end_frame() = 0;

    /// Present the frame through the attached platform surface.
    virtual void present(NativeSurface& surface) = 0;

    /// Renderer-side hotspot counters captured for the most recently rendered frame.
    [[nodiscard]] virtual RenderHotspotCounters last_hotspot_counters() const;

protected:
    Renderer();
};

/// A minimal software renderer that writes to a CPU pixel buffer.
/// Suitable for early development and testing.
class SoftwareRenderer : public Renderer {
public:
    SoftwareRenderer();
    ~SoftwareRenderer() override;

    [[nodiscard]] RendererBackend backend() const override;
    bool attach_surface(NativeSurface& surface) override;
    void set_damage_regions(std::span<const Rect> regions) override;
    void begin_frame(Size viewport, float scale_factor) override;
    void render(const RenderNode& root) override;
    void end_frame() override;
    void present(NativeSurface& surface) override;
    [[nodiscard]] RenderHotspotCounters last_hotspot_counters() const override;

    /// Set the text shaper used for rendering TextNode.
    void set_text_shaper(TextShaper* shaper) override;

    /// Access the pixel buffer after end_frame().
    /// Format: RGBA8, row-major, top-left origin.
    [[nodiscard]] const uint8_t* pixel_data() const;
    [[nodiscard]] int pixel_width() const;
    [[nodiscard]] int pixel_height() const;

private:
    [[nodiscard]] std::optional<std::span<const Rect>> present_damage_regions() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::unique_ptr<Renderer> create_renderer(RendererBackend backend);

} // namespace nk
