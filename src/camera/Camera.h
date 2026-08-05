#pragma once

#include "core/Math.h"

namespace tessera {

struct Ray {
    vec3 origin{0.0f};
    vec3 direction{0.0f, 0.0f, -1.0f};
};

/// Orbit camera with Blender-style controls.
///
/// State is kept as (target, distance, yaw, pitch) rather than a free matrix so
/// framing, view presets and turntable motion are all trivially expressible.
class Camera {
public:
    enum class Projection { Perspective, Orthographic };

    enum class View { Front, Back, Left, Right, Top, Bottom, Isometric };

    [[nodiscard]] mat4 view() const;
    [[nodiscard]] mat4 projection(float aspect) const;
    [[nodiscard]] mat4 viewProjection(float aspect) const { return projection(aspect) * view(); }
    [[nodiscard]] vec3 position() const;
    [[nodiscard]] vec3 forward() const;
    [[nodiscard]] vec3 right() const;
    [[nodiscard]] vec3 up() const;

    /// Moves the camera so `box` fills the viewport with a small margin.
    void frame(const Aabb& box, float aspect);

    void orbit(float deltaYaw, float deltaPitch);
    /// `dx`/`dy` are in pixels; panning speed follows the distance so the model
    /// tracks the cursor at any zoom level.
    void pan(float dx, float dy, float aspect, int viewportHeight);
    /// Positive zooms in. Multiplicative, so zooming feels the same at any scale.
    void dolly(float amount);

    void setView(View preset);

    /// Ray through a normalised device coordinate in [-1, 1].
    [[nodiscard]] Ray rayThrough(vec2 ndc, float aspect) const;

    /// Recomputes the near/far planes from the scene size. Called after loads
    /// and whenever the distance changes a lot, to keep depth precision usable
    /// across the huge range of scene scales real files come in.
    void updateClipPlanes(const Aabb& sceneBounds);

    vec3 target{0.0f};
    float distance = 5.0f;
    float yaw = -0.6f;     ///< radians, around +Y
    float pitch = 0.45f;   ///< radians, clamped away from the poles
    float fovDegrees = 45.0f;
    float nearPlane = 0.01f;
    float farPlane = 1000.0f;
    Projection projectionMode = Projection::Perspective;

    /// Extra spin applied by the turntable animation, in radians.
    float turntableSpeed = 0.0f;
};

}  // namespace tessera
