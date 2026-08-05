#pragma once

#include "gfx/opengl/GLCommon.h"
#include "scene/Scene.h"

namespace tessera::gfx {

/// GPU mirror of a `scene::Mesh`: one VAO with an interleaved vertex buffer and
/// an optional index buffer.
class GpuMesh {
public:
    GpuMesh() = default;
    ~GpuMesh();
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& other) noexcept;
    GpuMesh& operator=(GpuMesh&& other) noexcept;

    void upload(const scene::Mesh& mesh);
    void destroy();

    /// Draws with the mesh's own topology.
    void draw() const;
    /// Draws as GL_POINTS regardless of topology (used by the normals overlay).
    void drawPoints() const;

    [[nodiscard]] bool valid() const { return vao_ != 0; }
    [[nodiscard]] std::size_t byteSize() const { return byteSize_; }
    [[nodiscard]] std::size_t primitiveCount() const { return primitiveCount_; }
    [[nodiscard]] GLsizei vertexCount() const { return vertexCount_; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLenum mode_ = GL_TRIANGLES;
    GLsizei elementCount_ = 0;
    GLsizei vertexCount_ = 0;
    bool indexed_ = false;
    std::size_t byteSize_ = 0;
    std::size_t primitiveCount_ = 0;
};

}  // namespace tessera::gfx
