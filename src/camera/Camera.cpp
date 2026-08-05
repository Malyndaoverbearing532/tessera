#include "camera/Camera.h"

#include <algorithm>
#include <cmath>

namespace tessera {
namespace {
// Keeping pitch just short of vertical avoids the up-vector flipping at the poles.
constexpr float kPitchLimit = 1.5533f;  // ~89 degrees
}  // namespace

vec3 Camera::forward() const {
    return {-std::cos(pitch) * std::sin(yaw), -std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
}

vec3 Camera::position() const { return target - forward() * distance; }

vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), vec3(0.0f, 1.0f, 0.0f)));
}

vec3 Camera::up() const { return glm::normalize(glm::cross(right(), forward())); }

mat4 Camera::view() const { return glm::lookAt(position(), target, vec3(0.0f, 1.0f, 0.0f)); }

mat4 Camera::projection(float aspect) const {
    aspect = std::max(aspect, 1e-4f);
    if (projectionMode == Projection::Perspective) {
        return glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
    }
    // Match the perspective framing at the orbit distance so toggling the mode
    // does not change how large the model appears.
    const float halfHeight = distance * std::tan(glm::radians(fovDegrees) * 0.5f);
    const float halfWidth = halfHeight * aspect;
    return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -farPlane, farPlane);
}

void Camera::frame(const Aabb& box, float aspect) {
    if (!box.valid()) {
        target = vec3(0.0f);
        distance = 5.0f;
        return;
    }

    target = box.center();
    const float radius = std::max(box.radius(), 1e-4f);
    const float verticalFov = glm::radians(fovDegrees);
    const float horizontalFov = 2.0f * std::atan(std::tan(verticalFov * 0.5f) * std::max(aspect, 1e-4f));
    const float limitingFov = std::min(verticalFov, horizontalFov);

    // 1.25 leaves a comfortable margin around the bounding sphere.
    distance = (radius / std::sin(limitingFov * 0.5f)) * 1.25f;
    updateClipPlanes(box);
}

void Camera::orbit(float deltaYaw, float deltaPitch) {
    yaw += deltaYaw;
    pitch = std::clamp(pitch + deltaPitch, -kPitchLimit, kPitchLimit);
}

void Camera::pan(float dx, float dy, float aspect, int viewportHeight) {
    if (viewportHeight <= 0) return;
    // World units covered by one pixel at the target plane.
    const float worldPerPixel =
        2.0f * distance * std::tan(glm::radians(fovDegrees) * 0.5f) / static_cast<float>(viewportHeight);
    target += right() * (-dx * worldPerPixel) + up() * (dy * worldPerPixel);
}

void Camera::dolly(float amount) {
    distance *= std::exp(-amount * 0.15f);
    distance = std::clamp(distance, 1e-4f, 1e9f);
}

void Camera::setView(View preset) {
    switch (preset) {
        case View::Front:  yaw = 0.0f;                  pitch = 0.0f; break;
        case View::Back:   yaw = glm::pi<float>();      pitch = 0.0f; break;
        case View::Right:  yaw = glm::half_pi<float>(); pitch = 0.0f; break;
        case View::Left:   yaw = -glm::half_pi<float>();pitch = 0.0f; break;
        case View::Top:    yaw = 0.0f;                  pitch = kPitchLimit; break;
        case View::Bottom: yaw = 0.0f;                  pitch = -kPitchLimit; break;
        case View::Isometric: yaw = -0.7853f;           pitch = 0.6154f; break;
    }
}

Ray Camera::rayThrough(vec2 ndc, float aspect) const {
    const mat4 inverseViewProjection = glm::inverse(viewProjection(aspect));

    const vec4 nearPoint = inverseViewProjection * vec4(ndc.x, ndc.y, -1.0f, 1.0f);
    const vec4 farPoint = inverseViewProjection * vec4(ndc.x, ndc.y, 1.0f, 1.0f);

    const vec3 start = vec3(nearPoint) / nearPoint.w;
    const vec3 end = vec3(farPoint) / farPoint.w;
    return {start, glm::normalize(end - start)};
}

void Camera::updateClipPlanes(const Aabb& sceneBounds) {
    const float radius = sceneBounds.valid() ? std::max(sceneBounds.radius(), 1e-4f) : 1.0f;
    const float extent = std::max(distance + radius * 2.0f, radius * 4.0f);

    // Tie near to the scene scale: a fixed near plane either z-fights on large
    // scenes or clips through small ones.
    nearPlane = std::max(extent * 1e-4f, 1e-5f);
    farPlane = extent * 4.0f;
}

}  // namespace tessera
