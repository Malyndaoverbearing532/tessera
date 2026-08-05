#pragma once

#include "core/Math.h"

namespace tessera::gfx {

/// What the fragment shader outputs. Anything past `Shaded` is an inspection
/// view rather than a beauty render.
enum class ShadingMode : int {
    Shaded = 0,
    Clay,
    BaseColor,
    Normals,
    Tangents,
    Uv,
    Metallic,
    Roughness,
    Occlusion,
    VertexColor,
    Count
};

const char* shadingModeName(ShadingMode mode);

struct RenderSettings {
    ShadingMode shading = ShadingMode::Shaded;

    // Overlays
    bool wireframe = false;
    bool showNormals = false;
    bool showBounds = false;
    bool showGrid = true;
    bool showBackground = true;
    float normalLength = 0.03f;  ///< fraction of the scene radius
    float pointSize = 3.0f;

    // Material / texture switches
    bool useTextures = true;
    bool useVertexColors = true;
    bool doubleSidedOverride = false;
    bool flatShading = false;

    // Lighting
    float lightYaw = 0.6f;    ///< radians, relative to the camera
    float lightPitch = 0.5f;
    float lightIntensity = 3.0f;
    float ambientIntensity = 0.35f;
    bool lightFollowsCamera = true;

    // Tone mapping
    float exposure = 1.0f;
    bool tonemap = true;

    // Background
    vec3 backgroundTop{0.20f, 0.22f, 0.26f};
    vec3 backgroundBottom{0.06f, 0.07f, 0.09f};
    vec3 wireColor{0.10f, 0.10f, 0.12f};
    vec3 selectionColor{1.00f, 0.55f, 0.15f};
    vec3 gridColor{0.45f, 0.47f, 0.52f};
};

/// Per-frame counters shown in the stats panel.
struct FrameStats {
    int drawCalls = 0;
    std::size_t trianglesDrawn = 0;
    double gpuUploadSeconds = 0.0;
    std::size_t gpuBytes = 0;
};

}  // namespace tessera::gfx
