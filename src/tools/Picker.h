#pragma once

#include "camera/Camera.h"
#include "scene/Scene.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tessera::tools {

struct PickResult {
    int nodeIndex = -1;
    int meshIndex = -1;
    std::uint32_t triangleIndex = 0;
    float distance = 0.0f;
    vec3 position{0.0f};  ///< world-space hit point
    vec3 normal{0.0f};    ///< world-space geometric normal
};

/// Ray/scene intersection backed by a per-mesh BVH.
///
/// Trees are built on first use rather than at load time, so opening a large
/// model stays fast and only meshes the user actually clicks near pay the cost.
class Picker {
public:
    void setScene(const scene::Scene* scene);

    /// Nearest hit along `ray`, respecting `visible` (may be empty = all shown).
    [[nodiscard]] std::optional<PickResult> pick(const Ray& ray,
                                                 const std::vector<bool>& visible) const;

    /// Number of meshes with a tree built so far - shown in the stats panel.
    [[nodiscard]] std::size_t builtTreeCount() const { return trees_.size(); }

private:
    struct Node {
        Aabb bounds;
        std::uint32_t start = 0;  ///< first triangle in `order` (leaf only)
        std::uint32_t count = 0;  ///< 0 marks an interior node
        std::uint32_t right = 0;  ///< index of the right child
    };

    struct Tree {
        std::vector<Node> nodes;
        std::vector<std::uint32_t> order;  ///< triangle indices, spatially sorted
    };

    const Tree& treeFor(int meshIndex) const;
    static void build(const scene::Mesh& mesh, Tree& tree);

    const scene::Scene* scene_ = nullptr;
    mutable std::unordered_map<int, Tree> trees_;
};

}  // namespace tessera::tools
