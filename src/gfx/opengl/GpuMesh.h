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

    /// Uploads `mesh`, optionally baking a world transform into the vertex data.
    ///
    /// A viewer's scene does not move, so a mesh used by exactly one node can
    /// have its transform folded in once here instead of being sent as two
    /// uniform matrices on every draw of every frame.
    void upload(const scene::Mesh& mesh, const mat4& bakeTransform = mat4(1.0f));

    /// Uploads geometry already assembled on the CPU, used for merged batches.
    /// Always indexed triangles, because that is the only thing worth merging.
    void uploadMerged(const std::vector<scene::Vertex>& vertices,
                      const std::vector<std::uint32_t>& indices);

    void destroy();

    /// Draws with the mesh's own topology.
    void draw() const;
    /// Draws as GL_POINTS regardless of topology (used by the normals overlay).
    void drawPoints() const;

    /// Draws a slice of the index buffer. Lets one merged batch stand in for
    /// many meshes while still allowing individual ones to be hidden, culled or
    /// highlighted.
    void drawRange(GLsizei indexOffset, GLsizei indexCount) const;

    /// Draws a slice of the vertex buffer as points, for the normals overlay.
    void drawPointsRange(GLsizei vertexOffset, GLsizei vertexCount) const;

    /// Binds without drawing, for callers issuing several ranges in a row.
    void bind() const;

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
