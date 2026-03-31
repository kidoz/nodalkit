#pragma once

/// @file renderer.h
/// @brief Abstract renderer backend.

#include <nk/foundation/types.h>

#include <memory>

namespace nk {

class RenderNode;
class TextShaper;

/// Abstract renderer backend. The MVP ships a software renderer that
/// rasterizes render nodes to a pixel buffer. Future backends will
/// target Vulkan, OpenGL, or Metal.
class Renderer {
public:
    virtual ~Renderer();

    Renderer(Renderer const&) = delete;
    Renderer& operator=(Renderer const&) = delete;

    /// Begin a frame with the given viewport size.
    virtual void begin_frame(Size viewport) = 0;

    /// Render the given render-node tree.
    virtual void render(RenderNode const& root) = 0;

    /// End the frame and present.
    virtual void end_frame() = 0;

protected:
    Renderer();
};

/// A minimal software renderer that writes to a CPU pixel buffer.
/// Suitable for early development and testing.
class SoftwareRenderer : public Renderer {
public:
    SoftwareRenderer();
    ~SoftwareRenderer() override;

    void begin_frame(Size viewport) override;
    void render(RenderNode const& root) override;
    void end_frame() override;

    /// Set the text shaper used for rendering TextNode.
    void set_text_shaper(TextShaper* shaper);

    /// Access the pixel buffer after end_frame().
    /// Format: RGBA8, row-major, top-left origin.
    [[nodiscard]] uint8_t const* pixel_data() const;
    [[nodiscard]] int pixel_width() const;
    [[nodiscard]] int pixel_height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
