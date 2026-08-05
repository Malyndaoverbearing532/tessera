#include "scene/Scene.h"

#include <unordered_map>

namespace tessera::scene {

std::size_t Mesh::primitiveCount() const {
    const std::size_t count = indices.empty() ? vertices.size() : indices.size();
    switch (topology) {
        case Topology::Triangles: return count / 3;
        case Topology::Lines: return count / 2;
        case Topology::Points: return count;
    }
    return 0;
}

void Mesh::updateBounds() {
    bounds = {};
    for (const Vertex& v : vertices) bounds.expand(v.position);
}

void Mesh::generateNormals() {
    if (topology != Topology::Triangles || vertices.empty()) return;

    for (Vertex& v : vertices) v.normal = vec3(0.0f);

    const std::size_t count = indices.empty() ? vertices.size() : indices.size();
    for (std::size_t i = 0; i + 2 < count; i += 3) {
        const std::uint32_t i0 = indices.empty() ? static_cast<std::uint32_t>(i) : indices[i];
        const std::uint32_t i1 = indices.empty() ? static_cast<std::uint32_t>(i + 1) : indices[i + 1];
        const std::uint32_t i2 = indices.empty() ? static_cast<std::uint32_t>(i + 2) : indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

        const vec3& a = vertices[i0].position;
        const vec3& b = vertices[i1].position;
        const vec3& c = vertices[i2].position;
        // Un-normalised cross product weights each face by twice its area,
        // which is the cheap way to get area-weighted smoothing.
        const vec3 faceNormal = glm::cross(b - a, c - a);
        vertices[i0].normal += faceNormal;
        vertices[i1].normal += faceNormal;
        vertices[i2].normal += faceNormal;
    }

    for (Vertex& v : vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : vec3(0.0f, 1.0f, 0.0f);
    }
}

void Mesh::generateTangents() {
    if (topology != Topology::Triangles || vertices.empty()) return;

    std::vector<vec3> tan(vertices.size(), vec3(0.0f));
    std::vector<vec3> bitan(vertices.size(), vec3(0.0f));

    const std::size_t count = indices.empty() ? vertices.size() : indices.size();
    for (std::size_t i = 0; i + 2 < count; i += 3) {
        const std::uint32_t i0 = indices.empty() ? static_cast<std::uint32_t>(i) : indices[i];
        const std::uint32_t i1 = indices.empty() ? static_cast<std::uint32_t>(i + 1) : indices[i + 1];
        const std::uint32_t i2 = indices.empty() ? static_cast<std::uint32_t>(i + 2) : indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

        const vec3 e1 = vertices[i1].position - vertices[i0].position;
        const vec3 e2 = vertices[i2].position - vertices[i0].position;
        const vec2 d1 = vertices[i1].uv - vertices[i0].uv;
        const vec2 d2 = vertices[i2].uv - vertices[i0].uv;

        const float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-12f) continue;
        const float r = 1.0f / det;

        const vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        const vec3 b = (e2 * d1.x - e1 * d2.x) * r;
        for (std::uint32_t index : {i0, i1, i2}) {
            tan[index] += t;
            bitan[index] += b;
        }
    }

    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const vec3& n = vertices[i].normal;
        vec3 t = tan[i] - n * glm::dot(n, tan[i]);  // Gram-Schmidt
        const float len = glm::length(t);
        if (len < 1e-12f) {
            // Degenerate UVs: any vector orthogonal to the normal will do.
            t = std::abs(n.z) < 0.9f ? glm::normalize(glm::cross(n, vec3(0, 0, 1)))
                                     : glm::normalize(glm::cross(n, vec3(1, 0, 0)));
        } else {
            t /= len;
        }
        const float handedness = glm::dot(glm::cross(n, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
        vertices[i].tangent = vec4(t, handedness);
    }
}

void Scene::clear() {
    meshes.clear();
    materials.clear();
    images.clear();
    nodes.clear();
    sourcePath.clear();
    importerName.clear();
    stats = {};
}

int Scene::ensureRoot() {
    if (nodes.empty()) nodes.push_back(Node{.name = "root"});
    return 0;
}

int Scene::addNode(const std::string& name, int parent, const mat4& transform) {
    const int index = static_cast<int>(nodes.size());
    Node node;
    node.name = name;
    node.parent = parent;
    node.transform = transform;
    nodes.push_back(std::move(node));
    if (parent >= 0 && parent < index) nodes[static_cast<std::size_t>(parent)].children.push_back(index);
    return index;
}

mat4 Scene::worldTransform(int nodeIndex) const {
    mat4 result(1.0f);
    while (nodeIndex >= 0 && nodeIndex < static_cast<int>(nodes.size())) {
        const Node& node = nodes[static_cast<std::size_t>(nodeIndex)];
        result = node.transform * result;
        nodeIndex = node.parent;
    }
    return result;
}

Aabb Scene::bounds() const {
    Aabb box;
    forEachMeshInstance([&](int, int meshIndex, const mat4& world) {
        box.expand(meshes[static_cast<std::size_t>(meshIndex)].bounds.transformed(world));
    });
    return box;
}

void Scene::finalize(bool doGenerateNormals, bool doGenerateTangents) {
    ensureRoot();

    // An importer that produced meshes but attached none of them to the
    // hierarchy still needs them drawn, so adopt the orphans under the root.
    std::vector<bool> referenced(meshes.size(), false);
    for (const Node& node : nodes) {
        for (int mesh : node.meshes) {
            if (mesh >= 0 && mesh < static_cast<int>(meshes.size())) {
                referenced[static_cast<std::size_t>(mesh)] = true;
            }
        }
    }
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        if (!referenced[i]) nodes[0].meshes.push_back(static_cast<int>(i));
    }

    stats.vertexCount = 0;
    stats.triangleCount = 0;
    for (Mesh& mesh : meshes) {
        const bool missingNormals =
            mesh.vertices.empty() ||
            glm::length(mesh.vertices.front().normal) < 1e-6f;
        if (doGenerateNormals && missingNormals) mesh.generateNormals();

        const bool needsTangents =
            mesh.material >= 0 && mesh.material < static_cast<int>(materials.size()) &&
            materials[static_cast<std::size_t>(mesh.material)].normalTexture >= 0;
        if (doGenerateTangents && needsTangents) mesh.generateTangents();

        mesh.updateBounds();
        stats.vertexCount += mesh.vertices.size();
        if (mesh.topology == Topology::Triangles) stats.triangleCount += mesh.primitiveCount();
    }

    stats.meshCount = meshes.size();
    stats.materialCount = materials.size();
    stats.imageCount = images.size();
    stats.nodeCount = nodes.size();
}

}  // namespace tessera::scene
