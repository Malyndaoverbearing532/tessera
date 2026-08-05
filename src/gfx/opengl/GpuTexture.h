#pragma once

#include "gfx/opengl/GLCommon.h"
#include "scene/Scene.h"

namespace tessera::gfx {

class GpuTexture {
public:
    GpuTexture() = default;
    ~GpuTexture();
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&& other) noexcept;
    GpuTexture& operator=(GpuTexture&& other) noexcept;

    void upload(const scene::Image& image);
    /// 1x1 texture used wherever a material slot is empty.
    void createSolid(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);
    void destroy();

    void bind(int unit) const;

    [[nodiscard]] bool valid() const { return texture_ != 0; }
    [[nodiscard]] std::size_t byteSize() const { return byteSize_; }

private:
    GLuint texture_ = 0;
    std::size_t byteSize_ = 0;
};

}  // namespace tessera::gfx
