#include "tools/Picker.h"

#include <algorithm>
#include <limits>
#include <numeric>

namespace tessera::tools {
namespace {

constexpr std::uint32_t kLeafSize = 8;

/// Triangle corner positions, handling both indexed and non-indexed meshes.
inline void triangleCorners(const scene::Mesh& mesh, std::uint32_t triangle, vec3& a, vec3& b,
                            vec3& c) {
    const std::size_t base = static_cast<std::size_t>(triangle) * 3;
    if (mesh.indices.empty()) {
        a = mesh.vertices[base].position;
        b = mesh.vertices[base + 1].position;
        c = mesh.vertices[base + 2].position;
    } else {
        a = mesh.vertices[mesh.indices[base]].position;
        b = mesh.vertices[mesh.indices[base + 1]].position;
        c = mesh.vertices[mesh.indices[base + 2]].position;
    }
}

}  // namespace

void Picker::setScene(const scene::Scene* scene) {
    scene_ = scene;
    trees_.clear();
}

void Picker::build(const scene::Mesh& mesh, Tree& tree) {
    const std::size_t triangleCount = mesh.primitiveCount();
    tree.order.resize(triangleCount);
    std::iota(tree.order.begin(), tree.order.end(), 0u);
    if (triangleCount == 0) return;

    // Centroids drive the split; caching them avoids re-reading vertices at
    // every level of the recursion.
    std::vector<vec3> centroids(triangleCount);
    std::vector<Aabb> boxes(triangleCount);
    for (std::size_t t = 0; t < triangleCount; ++t) {
        vec3 a, b, c;
        triangleCorners(mesh, static_cast<std::uint32_t>(t), a, b, c);
        Aabb box;
        box.expand(a);
        box.expand(b);
        box.expand(c);
        boxes[t] = box;
        centroids[t] = (a + b + c) / 3.0f;
    }

    tree.nodes.reserve(triangleCount * 2 / kLeafSize + 4);

    // Explicit stack: recursion would risk blowing up on degenerate meshes.
    struct Task {
        std::uint32_t nodeIndex;
        std::uint32_t start;
        std::uint32_t count;
    };
    tree.nodes.push_back({});
    std::vector<Task> stack{{0u, 0u, static_cast<std::uint32_t>(triangleCount)}};

    while (!stack.empty()) {
        const Task task = stack.back();
        stack.pop_back();

        Aabb bounds;
        for (std::uint32_t i = 0; i < task.count; ++i) {
            bounds.expand(boxes[tree.order[task.start + i]]);
        }
        tree.nodes[task.nodeIndex].bounds = bounds;

        if (task.count <= kLeafSize) {
            tree.nodes[task.nodeIndex].start = task.start;
            tree.nodes[task.nodeIndex].count = task.count;
            continue;
        }

        // Split at the median along the widest axis of the centroid spread.
        Aabb centroidBounds;
        for (std::uint32_t i = 0; i < task.count; ++i) {
            centroidBounds.expand(centroids[tree.order[task.start + i]]);
        }
        const vec3 extent = centroidBounds.size();
        const int axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2)
                                            : (extent.y > extent.z ? 1 : 2);

        const auto begin = tree.order.begin() + task.start;
        const auto middle = begin + task.count / 2;
        std::nth_element(begin, middle, begin + task.count,
                         [&](std::uint32_t lhs, std::uint32_t rhs) {
                             return centroids[lhs][axis] < centroids[rhs][axis];
                         });

        const std::uint32_t leftCount = task.count / 2;
        const auto leftIndex = static_cast<std::uint32_t>(tree.nodes.size());
        tree.nodes.push_back({});
        const auto rightIndex = static_cast<std::uint32_t>(tree.nodes.size());
        tree.nodes.push_back({});

        tree.nodes[task.nodeIndex].count = 0;
        tree.nodes[task.nodeIndex].right = rightIndex;

        stack.push_back({leftIndex, task.start, leftCount});
        stack.push_back({rightIndex, task.start + leftCount, task.count - leftCount});
    }
}

const Picker::Tree& Picker::treeFor(int meshIndex) const {
    if (const auto it = trees_.find(meshIndex); it != trees_.end()) return it->second;
    Tree tree;
    build(scene_->meshes[static_cast<std::size_t>(meshIndex)], tree);
    return trees_.emplace(meshIndex, std::move(tree)).first->second;
}

std::optional<PickResult> Picker::pick(const Ray& ray, const std::vector<bool>& visible) const {
    if (!scene_) return std::nullopt;

    std::optional<PickResult> best;
    float bestDistance = std::numeric_limits<float>::max();

    scene_->forEachMeshInstance([&](int nodeIndex, int meshIndex, const mat4& world) {
        if (!visible.empty() && meshIndex < static_cast<int>(visible.size()) &&
            !visible[static_cast<std::size_t>(meshIndex)]) {
            return;
        }
        const scene::Mesh& mesh = scene_->meshes[static_cast<std::size_t>(meshIndex)];
        if (mesh.topology != scene::Topology::Triangles || mesh.vertices.empty()) return;

        // Intersect in object space so the BVH is built once per mesh, not once
        // per instance.
        const mat4 inverseWorld = glm::inverse(world);
        const vec3 origin = vec3(inverseWorld * vec4(ray.origin, 1.0f));
        const vec3 direction = vec3(inverseWorld * vec4(ray.direction, 0.0f));
        const float directionLength = glm::length(direction);
        if (directionLength < 1e-12f) return;
        const vec3 unitDirection = direction / directionLength;

        // Guard against division by zero in the slab test.
        const vec3 inverseDirection(
            1.0f / (std::abs(unitDirection.x) < 1e-9f ? 1e-9f : unitDirection.x),
            1.0f / (std::abs(unitDirection.y) < 1e-9f ? 1e-9f : unitDirection.y),
            1.0f / (std::abs(unitDirection.z) < 1e-9f ? 1e-9f : unitDirection.z));

        if (!mesh.bounds.intersectRay(origin, inverseDirection,
                                      std::numeric_limits<float>::max())) {
            return;
        }

        const Tree& tree = treeFor(meshIndex);
        if (tree.nodes.empty()) return;

        float localBest = std::numeric_limits<float>::max();
        std::uint32_t hitTriangle = 0;
        bool hit = false;

        std::vector<std::uint32_t> stack{0u};
        while (!stack.empty()) {
            const std::uint32_t nodeId = stack.back();
            stack.pop_back();
            const Node& node = tree.nodes[nodeId];

            float entry = 0.0f;
            if (!node.bounds.intersectRay(origin, inverseDirection, localBest, &entry)) continue;
            if (entry > localBest) continue;

            if (node.count == 0) {
                stack.push_back(nodeId + 1);   // left child sits right after the parent
                stack.push_back(node.right);
                continue;
            }

            for (std::uint32_t i = 0; i < node.count; ++i) {
                const std::uint32_t triangle = tree.order[node.start + i];
                vec3 a, b, c;
                triangleCorners(mesh, triangle, a, b, c);
                const float t = intersectTriangle(origin, unitDirection, a, b, c);
                if (t > 1e-6f && t < localBest) {
                    localBest = t;
                    hitTriangle = triangle;
                    hit = true;
                }
            }
        }

        if (!hit) return;

        // Convert the object-space parameter back to a world-space distance.
        const vec3 localPoint = origin + unitDirection * localBest;
        const vec3 worldPoint = vec3(world * vec4(localPoint, 1.0f));
        const float worldDistance = glm::length(worldPoint - ray.origin);
        if (worldDistance >= bestDistance) return;

        vec3 a, b, c;
        triangleCorners(mesh, hitTriangle, a, b, c);
        const mat3 normalMatrix = glm::transpose(glm::inverse(mat3(world)));

        PickResult result;
        result.nodeIndex = nodeIndex;
        result.meshIndex = meshIndex;
        result.triangleIndex = hitTriangle;
        result.distance = worldDistance;
        result.position = worldPoint;
        result.normal = glm::normalize(normalMatrix * glm::cross(b - a, c - a));

        bestDistance = worldDistance;
        best = result;
    });

    return best;
}

}  // namespace tessera::tools
