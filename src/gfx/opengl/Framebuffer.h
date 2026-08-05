#pragma once

#include "gfx/opengl/GLCommon.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tessera::gfx {

/// Offscreen render target with an optional multisample pass and a resolve
/// buffer to read back from. Used for screenshots and for headless rendering,
/// where there is no default framebuffer worth reading.
class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer();
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    /// (Re)allocates for the given size. `samples` <= 1 disables multisampling.
    bool create(int width, int height, int samples, std::string& error);
    void destroy();

    /// Binds for rendering and sets the viewport.
    void bind() const;
    static void bindDefault(int width, int height);

    /// Resolves multisample data into the readable attachment.
    void resolve() const;

    /// Reads the resolved image back as tightly packed RGBA8, top row first.
    bool readPixels(std::vector<std::uint8_t>& out) const;

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] bool valid() const { return resolveFbo_ != 0; }

private:
    int width_ = 0;
    int height_ = 0;
    int samples_ = 1;

    GLuint msaaFbo_ = 0;
    GLuint msaaColor_ = 0;
    GLuint msaaDepth_ = 0;

    GLuint resolveFbo_ = 0;
    GLuint resolveColor_ = 0;
    GLuint resolveDepth_ = 0;
};

}  // namespace tessera::gfx
