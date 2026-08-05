#include "gfx/opengl/GpuMesh.h"

#include <utility>

namespace tessera::gfx {

GpuMesh::~GpuMesh() { destroy(); }

GpuMesh::GpuMesh(GpuMesh&& other) noexcept
    : vao_(other.vao_),
      vbo_(other.vbo_),
      ebo_(other.ebo_),
      mode_(other.mode_),
      elementCount_(other.elementCount_),
      vertexCount_(other.vertexCount_),
      indexed_(other.indexed_),
      byteSize_(other.byteSize_),
      primitiveCount_(other.primitiveCount_) {
    other.vao_ = other.vbo_ = other.ebo_ = 0;
}

GpuMesh& GpuMesh::operator=(GpuMesh&& other) noexcept {
    if (this != &other) {
        destroy();
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        ebo_ = std::exchange(other.ebo_, 0);
        mode_ = other.mode_;
        elementCount_ = other.elementCount_;
        vertexCount_ = other.vertexCount_;
        indexed_ = other.indexed_;
        byteSize_ = other.byteSize_;
        primitiveCount_ = other.primitiveCount_;
    }
    return *this;
}

void GpuMesh::destroy() {
    if (ebo_ != 0) glDeleteBuffers(1, &ebo_);
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
    elementCount_ = 0;
    vertexCount_ = 0;
    byteSize_ = 0;
    primitiveCount_ = 0;
}

void GpuMesh::upload(const scene::Mesh& mesh) {
    destroy();
    if (mesh.vertices.empty()) return;

    switch (mesh.topology) {
        case scene::Topology::Triangles: mode_ = GL_TRIANGLES; break;
        case scene::Topology::Lines: mode_ = GL_LINES; break;
        case scene::Topology::Points: mode_ = GL_POINTS; break;
    }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    const std::size_t vertexBytes = mesh.vertices.size() * sizeof(scene::Vertex);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexBytes), mesh.vertices.data(),
                 GL_STATIC_DRAW);

    const auto stride = static_cast<GLsizei>(sizeof(scene::Vertex));
    const auto offsetOf = [](std::size_t bytes) { return reinterpret_cast<const void*>(bytes); };

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, offsetOf(offsetof(scene::Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, offsetOf(offsetof(scene::Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, offsetOf(offsetof(scene::Vertex, tangent)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, offsetOf(offsetof(scene::Vertex, uv)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, offsetOf(offsetof(scene::Vertex, color)));

    std::size_t indexBytes = 0;
    if (!mesh.indices.empty()) {
        indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
        glGenBuffers(1, &ebo_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indexBytes),
                     mesh.indices.data(), GL_STATIC_DRAW);
        indexed_ = true;
        elementCount_ = static_cast<GLsizei>(mesh.indices.size());
    } else {
        indexed_ = false;
        elementCount_ = static_cast<GLsizei>(mesh.vertices.size());
    }

    glBindVertexArray(0);

    vertexCount_ = static_cast<GLsizei>(mesh.vertices.size());
    byteSize_ = vertexBytes + indexBytes;
    primitiveCount_ = mesh.primitiveCount();
}

void GpuMesh::draw() const {
    if (vao_ == 0 || elementCount_ == 0) return;
    glBindVertexArray(vao_);
    if (indexed_) {
        glDrawElements(mode_, elementCount_, GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(mode_, 0, elementCount_);
    }
}

void GpuMesh::drawPoints() const {
    if (vao_ == 0 || vertexCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, vertexCount_);
}

}  // namespace tessera::gfx
