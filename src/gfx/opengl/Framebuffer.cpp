#include "gfx/opengl/Framebuffer.h"

#include <algorithm>
#include <format>

namespace tessera::gfx {

Framebuffer::~Framebuffer() { destroy(); }

void Framebuffer::destroy() {
    if (msaaColor_ != 0) glDeleteRenderbuffers(1, &msaaColor_);
    if (msaaDepth_ != 0) glDeleteRenderbuffers(1, &msaaDepth_);
    if (msaaFbo_ != 0) glDeleteFramebuffers(1, &msaaFbo_);
    if (resolveDepth_ != 0) glDeleteRenderbuffers(1, &resolveDepth_);
    if (resolveColor_ != 0) glDeleteTextures(1, &resolveColor_);
    if (resolveFbo_ != 0) glDeleteFramebuffers(1, &resolveFbo_);

    msaaFbo_ = msaaColor_ = msaaDepth_ = 0;
    resolveFbo_ = resolveColor_ = resolveDepth_ = 0;
    width_ = height_ = 0;
}

bool Framebuffer::create(int width, int height, int samples, std::string& error) {
    destroy();

    width_ = std::max(width, 1);
    height_ = std::max(height, 1);

    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::clamp(samples, 1, std::max(1, maxSamples));

    // Resolve target: a plain texture we can sample and read back.
    glGenFramebuffers(1, &resolveFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);

    glGenTextures(1, &resolveColor_);
    glBindTexture(GL_TEXTURE_2D, resolveColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveColor_, 0);

    glGenRenderbuffers(1, &resolveDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, resolveDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width_, height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                              resolveDepth_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        error = "resolve framebuffer is incomplete";
        destroy();
        return false;
    }

    if (samples_ > 1) {
        glGenFramebuffers(1, &msaaFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);

        glGenRenderbuffers(1, &msaaColor_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaColor_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColor_);

        glGenRenderbuffers(1, &msaaDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH24_STENCIL8, width_,
                                         height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                  msaaDepth_);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            // Fall back to single-sampled rendering rather than failing outright.
            glDeleteRenderbuffers(1, &msaaColor_);
            glDeleteRenderbuffers(1, &msaaDepth_);
            glDeleteFramebuffers(1, &msaaFbo_);
            msaaFbo_ = msaaColor_ = msaaDepth_ = 0;
            samples_ = 1;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, samples_ > 1 ? msaaFbo_ : resolveFbo_);
    glViewport(0, 0, width_, height_);
}

void Framebuffer::bindDefault(int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, std::max(width, 1), std::max(height, 1));
}

void Framebuffer::resolve() const {
    if (samples_ <= 1 || msaaFbo_ == 0) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Framebuffer::readPixels(std::vector<std::uint8_t>& out) const {
    if (resolveFbo_ == 0) return false;

    out.resize(static_cast<std::size_t>(width_) * height_ * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return true;
}

}  // namespace tessera::gfx
