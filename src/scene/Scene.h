#pragma once

#include "core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tessera::scene {

/// Interleaved vertex layout shared by every importer, so the GPU upload path
/// never has to know which file format the data came from.
struct Vertex {
    vec3 position{0.0f};
    vec3 normal{0.0f, 1.0f, 0.0f};
    vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};  // w = bitangent handedness
    vec2 uv{0.0f};
    vec4 color{1.0f};
};

enum class Topology : std::uint8_t { Triangles, Lines, Points };

enum class AlphaMode : std::uint8_t { Opaque, Mask, Blend };

/// A decoded image. Kept on the CPU so headless/convert runs never touch the GPU.
struct Image {
    std::string name;
    int width = 0;
    int height = 0;
    int channels = 0;
    bool srgb = false;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool empty() const { return pixels.empty() || width <= 0 || height <= 0; }
};

/// Metallic-roughness material. Texture members index into Scene::images,
/// with -1 meaning "no texture".
struct Material {
    std::string name = "material";
    vec4 baseColor{1.0f};
    vec3 emissive{0.0f};
    float metallic = 0.0f;
    float roughness = 0.75f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;

    int baseColorTexture = -1;
    int metallicRoughnessTexture = -1;
    int normalTexture = -1;
    int emissiveTexture = -1;
    int occlusionTexture = -1;
};

struct Mesh {
    std::string name = "mesh";
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    int material = -1;
    Topology topology = Topology::Triangles;
    Aabb bounds;

    [[nodiscard]] std::size_t primitiveCount() const;
    /// Recomputes `bounds` from the vertex positions.
    void updateBounds();
    /// Area-weighted smooth normals; only touches meshes that have none.
    void generateNormals();
    /// Per-vertex tangents from UVs (Lengyel's method). No-op without UVs.
    void generateTangents();
};

/// Transform hierarchy node. Indices refer to Scene arrays; `parent` is -1 for
/// the root.
struct Node {
    std::string name = "node";
    mat4 transform{1.0f};
    int parent = -1;
    std::vector<int> children;
    std::vector<int> meshes;
    bool visible = true;
};

struct Stats {
    std::size_t vertexCount = 0;
    std::size_t triangleCount = 0;
    std::size_t meshCount = 0;
    std::size_t materialCount = 0;
    std::size_t imageCount = 0;
    std::size_t nodeCount = 0;
    double loadSeconds = 0.0;
};

/// The intermediate representation every importer produces and every consumer
/// (renderer, exporter, picker) reads. Deliberately plain data.
class Scene {
public:
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Image> images;
    std::vector<Node> nodes;

    std::string sourcePath;
    std::string importerName;
    Stats stats;

    [[nodiscard]] bool empty() const { return meshes.empty(); }
    void clear();

    /// Creates the root if the scene has no nodes yet. Returns its index (0).
    int ensureRoot();
    int addNode(const std::string& name, int parent, const mat4& transform = mat4(1.0f));

    /// Accumulated parent transforms for `nodeIndex`.
    [[nodiscard]] mat4 worldTransform(int nodeIndex) const;

    /// World-space bounds of every visible mesh instance.
    [[nodiscard]] Aabb bounds() const;

    /// Runs post-import fixups: bounds, missing normals/tangents, stats.
    void finalize(bool generateNormals, bool generateTangents);

    /// Calls `fn(nodeIndex, meshIndex, worldTransform)` for each visible mesh
    /// instance, depth-first. This is the single traversal every consumer uses.
    template <class Fn>
    void forEachMeshInstance(Fn&& fn) const {
        if (nodes.empty()) {
            // Importers may emit a flat mesh list without a hierarchy.
            for (int i = 0; i < static_cast<int>(meshes.size()); ++i) fn(-1, i, mat4(1.0f));
            return;
        }
        visitInstances(0, mat4(1.0f), fn);
    }

private:
    template <class Fn>
    void visitInstances(int nodeIndex, const mat4& parentTransform, Fn& fn) const {
        const Node& node = nodes[static_cast<std::size_t>(nodeIndex)];
        if (!node.visible) return;
        const mat4 world = parentTransform * node.transform;
        for (int mesh : node.meshes) fn(nodeIndex, mesh, world);
        for (int child : node.children) visitInstances(child, world, fn);
    }
};

}  // namespace tessera::scene
