#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <limits>

namespace tessera {

using glm::mat3;
using glm::mat4;
using glm::quat;
using glm::vec2;
using glm::vec3;
using glm::vec4;

/// Axis-aligned bounding box. An empty box has min > max, which makes
/// `expand()` work correctly from a default-constructed value.
struct Aabb {
    vec3 min{std::numeric_limits<float>::max()};
    vec3 max{std::numeric_limits<float>::lowest()};

    [[nodiscard]] bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    [[nodiscard]] vec3 center() const { return valid() ? (min + max) * 0.5f : vec3(0.0f); }
    [[nodiscard]] vec3 size() const { return valid() ? max - min : vec3(0.0f); }
    [[nodiscard]] float radius() const { return valid() ? glm::length(size()) * 0.5f : 1.0f; }
    [[nodiscard]] float longestEdge() const {
        const vec3 s = size();
        return std::max({s.x, s.y, s.z});
    }

    void expand(const vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void expand(const Aabb& other) {
        if (!other.valid()) return;
        expand(other.min);
        expand(other.max);
    }

    /// Bounding box of this box transformed by `m` (transforms all 8 corners).
    [[nodiscard]] Aabb transformed(const mat4& m) const {
        if (!valid()) return {};
        Aabb out;
        for (int i = 0; i < 8; ++i) {
            const vec3 corner{(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y,
                              (i & 4) ? max.z : min.z};
            out.expand(vec3(m * vec4(corner, 1.0f)));
        }
        return out;
    }

    /// Slab test. Returns true and writes the entry distance when the ray hits.
    [[nodiscard]] bool intersectRay(const vec3& origin, const vec3& invDir, float tMax,
                                    float* tHit = nullptr) const {
        const vec3 t0 = (min - origin) * invDir;
        const vec3 t1 = (max - origin) * invDir;
        const vec3 lo = glm::min(t0, t1);
        const vec3 hi = glm::max(t0, t1);
        const float tNear = std::max({lo.x, lo.y, lo.z, 0.0f});
        const float tFar = std::min({hi.x, hi.y, hi.z, tMax});
        if (tNear > tFar) return false;
        if (tHit) *tHit = tNear;
        return true;
    }
};

/// The six clipping planes of a view-projection matrix, each stored as
/// (normal.xyz, distance) and pointing inwards.
struct Frustum {
    vec4 planes[6];

    /// Gribb and Hartmann's extraction: the planes fall straight out of sums
    /// and differences of the matrix rows, with no inverse required.
    void extract(const mat4& viewProjection) {
        const mat4& m = viewProjection;
        // glm is column-major, so m[column][row]; these gather the rows.
        const vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
        const vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
        const vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
        const vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

        planes[0] = row3 + row0;  // left
        planes[1] = row3 - row0;  // right
        planes[2] = row3 + row1;  // bottom
        planes[3] = row3 - row1;  // top
        planes[4] = row3 + row2;  // near
        planes[5] = row3 - row2;  // far

        for (vec4& plane : planes) {
            const float length = glm::length(vec3(plane));
            if (length > 1e-12f) plane /= length;
        }
    }

    /// Conservative test: rejects only boxes wholly outside a plane. Testing
    /// the corner furthest along each normal is enough, and costs one dot
    /// product per plane.
    [[nodiscard]] bool intersects(const Aabb& box) const {
        if (!box.valid()) return true;  // no bounds means never cull
        for (const vec4& plane : planes) {
            const vec3 normal(plane);
            const vec3 furthest(normal.x >= 0.0f ? box.max.x : box.min.x,
                                normal.y >= 0.0f ? box.max.y : box.min.y,
                                normal.z >= 0.0f ? box.max.z : box.min.z);
            if (glm::dot(normal, furthest) + plane.w < 0.0f) return false;
        }
        return true;
    }
};

/// Möller-Trumbore. Returns the ray parameter, or a negative value on a miss.
inline float intersectTriangle(const vec3& origin, const vec3& dir, const vec3& a, const vec3& b,
                               const vec3& c) {
    constexpr float kEpsilon = 1e-8f;
    const vec3 e1 = b - a;
    const vec3 e2 = c - a;
    const vec3 pv = glm::cross(dir, e2);
    const float det = glm::dot(e1, pv);
    if (std::abs(det) < kEpsilon) return -1.0f;

    const float invDet = 1.0f / det;
    const vec3 tv = origin - a;
    const float u = glm::dot(tv, pv) * invDet;
    if (u < 0.0f || u > 1.0f) return -1.0f;

    const vec3 qv = glm::cross(tv, e1);
    const float v = glm::dot(dir, qv) * invDet;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;

    return glm::dot(e2, qv) * invDet;
}

}  // namespace tessera
