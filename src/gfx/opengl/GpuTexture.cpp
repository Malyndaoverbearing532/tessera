#include "gfx/opengl/GpuTexture.h"

#include <algorithm>
#include <utility>

namespace tessera::gfx {
namespace {

/// Maps channel count to (internal, external) formats. Colour textures use the
/// sRGB internal formats so the hardware linearises them for free.
void formatsFor(int channels, bool srgb, GLenum& internalFormat, GLenum& format) {
    switch (channels) {
        case 1:
            internalFormat = GL_RED;
            format = GL_RED;
            break;
        case 2:
            internalFormat = GL_RG;
            format = GL_RG;
            break;
        case 3:
            internalFormat = srgb ? GL_SRGB8 : GL_RGB8;
            format = GL_RGB;
            break;
        default:
            internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            format = GL_RGBA;
            break;
    }
}

}  // namespace

GpuTexture::~GpuTexture() { destroy(); }

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : texture_(std::exchange(other.texture_, 0)), byteSize_(other.byteSize_) {}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        destroy();
        texture_ = std::exchange(other.texture_, 0);
        byteSize_ = other.byteSize_;
    }
    return *this;
}

void GpuTexture::destroy() {
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    byteSize_ = 0;
}

void GpuTexture::upload(const scene::Image& image) {
    destroy();
    if (image.empty()) return;

    GLenum internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    formatsFor(image.channels, image.srgb, internalFormat, format);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);

    // Tightly packed rows: stb hands back byte-aligned data, and a 3-channel
    // image of odd width breaks under the default 4-byte alignment.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), image.width, image.height, 0,
                 format, GL_UNSIGNED_BYTE, image.pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        GLfloat maxAnisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        std::min(8.0f, maxAnisotropy));
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    byteSize_ = image.pixels.size() * 4 / 3;  // rough allowance for the mip chain
}

void GpuTexture::createSolid(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    destroy();
    const std::uint8_t pixel[4] = {r, g, b, a};

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    byteSize_ = 4;
}

void GpuTexture::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, texture_);
}

}  // namespace tessera::gfx
