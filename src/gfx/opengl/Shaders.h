#pragma once

// GLSL sources, embedded so the binary is self-contained and there is no
// runtime asset path to get wrong. Targets GL 3.3 core, which is the highest
// common denominator across macOS, Linux and Windows.

namespace tessera::gfx::shaders {

inline constexpr const char* kMeshVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUv;
layout(location = 4) in vec4 aColor;

uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat3 uNormalMatrix;
uniform float uPointSize;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUv;
out vec4 vColor;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vTangent = vec4(uNormalMatrix * aTangent.xyz, aTangent.w);
    vUv = aUv;
    vColor = aColor;
    gl_PointSize = uPointSize;
    gl_Position = uViewProjection * world;
}
)";

inline constexpr const char* kMeshFragment = R"(#version 330 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUv;
in vec4 vColor;

out vec4 fragColor;

uniform vec3 uCameraPosition;

// Material
uniform vec4  uBaseColor;
uniform vec3  uEmissive;
uniform float uMetallic;
uniform float uRoughness;
uniform float uNormalScale;
uniform float uOcclusionStrength;
uniform float uAlphaCutoff;
uniform int   uAlphaMode;      // 0 opaque, 1 mask, 2 blend

uniform sampler2D uBaseColorMap;
uniform sampler2D uMetallicRoughnessMap;
uniform sampler2D uNormalMap;
uniform sampler2D uEmissiveMap;
uniform sampler2D uOcclusionMap;
uniform int uHasBaseColorMap;
uniform int uHasMetallicRoughnessMap;
uniform int uHasNormalMap;
uniform int uHasEmissiveMap;
uniform int uHasOcclusionMap;

// Lighting
uniform vec3  uLightDirection[3];
uniform vec3  uLightColor[3];
uniform vec3  uAmbientTop;
uniform vec3  uAmbientBottom;
uniform float uAmbientIntensity;

// Presentation
uniform int   uShadingMode;
uniform int   uUseVertexColors;
uniform int   uFlatShading;
uniform float uExposure;
uniform int   uTonemap;

const float kPi = 3.14159265359;

float distributionGGX(float nDotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, 1e-7);
}

float geometrySmith(float nDotV, float nDotL, float roughness) {
    // Schlick-GGX with the direct-lighting k remapping.
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = nDotV / (nDotV * (1.0 - k) + k);
    float gl = nDotL / (nDotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
    vec3 maxReflect = max(vec3(1.0 - roughness), f0);
    return f0 + (maxReflect - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Hemispheric environment standing in for a full IBL probe: cheap, stable, and
// enough to keep metals from reading as black.
vec3 environment(vec3 direction) {
    float t = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(uAmbientBottom, uAmbientTop, t) * uAmbientIntensity;
}

vec3 acesFilmic(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 shadingNormal(vec3 geometricNormal) {
    if (uHasNormalMap == 0) return geometricNormal;

    vec3 t = normalize(vTangent.xyz - geometricNormal * dot(geometricNormal, vTangent.xyz));
    if (any(isnan(t)) || length(vTangent.xyz) < 1e-6) return geometricNormal;
    vec3 b = cross(geometricNormal, t) * (vTangent.w < 0.0 ? -1.0 : 1.0);

    vec3 sampled = texture(uNormalMap, vUv).xyz * 2.0 - 1.0;
    sampled.xy *= uNormalScale;
    return normalize(mat3(t, b, geometricNormal) * sampled);
}

void main() {
    vec3 geometricNormal;
    if (dot(vNormal, vNormal) < 1e-12) {
        // Point clouds and line sets have no meaningful normal. Facing the
        // camera makes them shade like little discs; normalising a zero vector
        // would hand every fragment a NaN and render the model black.
        geometricNormal = normalize(uCameraPosition - vWorldPosition);
    } else {
        geometricNormal = normalize(vNormal);
    }
    if (uFlatShading == 1) {
        // Screen-space derivatives give faceted normals without a second mesh.
        geometricNormal = normalize(cross(dFdx(vWorldPosition), dFdy(vWorldPosition)));
    }
    if (!gl_FrontFacing) geometricNormal = -geometricNormal;

    vec4 albedo = uBaseColor;
    if (uHasBaseColorMap == 1) albedo *= texture(uBaseColorMap, vUv);
    if (uUseVertexColors == 1) albedo *= vColor;

    if (uAlphaMode == 1 && albedo.a < uAlphaCutoff) discard;

    float metallic = uMetallic;
    float roughness = uRoughness;
    if (uHasMetallicRoughnessMap == 1) {
        // glTF packing: G = roughness, B = metallic. Not named `packed`:
        // that is a reserved GLSL keyword, which Apple's compiler tolerates
        // and Mesa rightly rejects.
        vec3 sampled = texture(uMetallicRoughnessMap, vUv).rgb;
        roughness *= sampled.g;
        metallic *= sampled.b;
    }
    roughness = clamp(roughness, 0.03, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

    float occlusion = 1.0;
    if (uHasOcclusionMap == 1) {
        occlusion = mix(1.0, texture(uOcclusionMap, vUv).r, uOcclusionStrength);
    }

    vec3 emissive = uEmissive;
    if (uHasEmissiveMap == 1) emissive *= texture(uEmissiveMap, vUv).rgb;

    vec3 normal = shadingNormal(geometricNormal);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    float nDotV = max(dot(normal, viewDirection), 1e-4);

    // ---- inspection modes -------------------------------------------------
    if (uShadingMode == 2) { fragColor = vec4(albedo.rgb, 1.0); return; }
    if (uShadingMode == 3) { fragColor = vec4(normal * 0.5 + 0.5, 1.0); return; }
    if (uShadingMode == 4) { fragColor = vec4(normalize(vTangent.xyz) * 0.5 + 0.5, 1.0); return; }
    if (uShadingMode == 5) {
        vec2 grid = abs(fract(vUv * 10.0) - 0.5);
        float checker = mod(floor(vUv.x * 10.0) + floor(vUv.y * 10.0), 2.0);
        fragColor = vec4(mix(vec3(0.25), vec3(0.85), checker) * vec3(vUv.x, vUv.y, 0.5) * 2.0, 1.0);
        return;
    }
    if (uShadingMode == 6) { fragColor = vec4(vec3(metallic), 1.0); return; }
    if (uShadingMode == 7) { fragColor = vec4(vec3(roughness), 1.0); return; }
    if (uShadingMode == 8) { fragColor = vec4(vec3(occlusion), 1.0); return; }
    if (uShadingMode == 9) { fragColor = vec4(vColor.rgb, 1.0); return; }

    // Clay: neutral material, keeps only shape information.
    if (uShadingMode == 1) {
        albedo = vec4(0.78, 0.76, 0.72, 1.0);
        metallic = 0.0;
        roughness = 0.65;
        emissive = vec3(0.0);
        occlusion = 1.0;
    }

    // ---- Cook-Torrance ----------------------------------------------------
    vec3 f0 = mix(vec3(0.04), albedo.rgb, metallic);
    vec3 direct = vec3(0.0);

    for (int i = 0; i < 3; ++i) {
        vec3 lightDirection = normalize(-uLightDirection[i]);
        float nDotL = max(dot(normal, lightDirection), 0.0);
        if (nDotL <= 0.0) continue;

        vec3 halfway = normalize(viewDirection + lightDirection);
        float nDotH = max(dot(normal, halfway), 0.0);

        float ndf = distributionGGX(nDotH, roughness);
        float geometry = geometrySmith(nDotV, nDotL, roughness);
        vec3 fresnel = fresnelSchlick(max(dot(halfway, viewDirection), 0.0), f0);

        vec3 specular = (ndf * geometry * fresnel) / max(4.0 * nDotV * nDotL, 1e-6);
        vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo.rgb / kPi;

        direct += (diffuse + specular) * uLightColor[i] * nDotL;
    }

    // ---- ambient ----------------------------------------------------------
    vec3 fresnelAmbient = fresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 irradiance = environment(normal);
    vec3 reflection = environment(reflect(-viewDirection, normal));

    vec3 ambientDiffuse = (vec3(1.0) - fresnelAmbient) * (1.0 - metallic) * albedo.rgb * irradiance;
    vec3 ambientSpecular = reflection * fresnelAmbient * (1.0 - roughness * 0.7);
    vec3 ambient = (ambientDiffuse + ambientSpecular) * occlusion;

    vec3 color = (direct + ambient) * uExposure + emissive;
    if (uTonemap == 1) color = acesFilmic(color);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

    fragColor = vec4(color, uAlphaMode == 2 ? albedo.a : 1.0);
}
)";

// ---------------------------------------------------------------------------
// Flat-coloured lines: wireframe, bounding boxes, measurement markers.
// ---------------------------------------------------------------------------
inline constexpr const char* kLineVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform float uDepthBias;

void main() {
    vec4 clip = uViewProjection * uModel * vec4(aPosition, 1.0);
    // Nudge toward the viewer so overlays sit on top of the surface they trace.
    clip.z -= uDepthBias * clip.w;
    gl_Position = clip;
}
)";

inline constexpr const char* kLineFragment = R"(#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main() { fragColor = uColor; }
)";

// ---------------------------------------------------------------------------
// Vertex-normal visualisation: one line segment per vertex, built on the GPU.
// ---------------------------------------------------------------------------
inline constexpr const char* kNormalsVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;

uniform mat4 uModel;
uniform mat3 uNormalMatrix;

out vec3 vNormal;
out vec3 vTangent;

void main() {
    gl_Position = uModel * vec4(aPosition, 1.0);
    vNormal = normalize(uNormalMatrix * aNormal);
    vTangent = normalize(uNormalMatrix * aTangent.xyz);
}
)";

inline constexpr const char* kNormalsGeometry = R"(#version 330 core
layout(points) in;
layout(line_strip, max_vertices = 2) out;

in vec3 vNormal[];
in vec3 vTangent[];

uniform mat4 uViewProjection;
uniform float uLength;
uniform int uShowTangents;

void main() {
    vec3 direction = uShowTangents == 1 ? vTangent[0] : vNormal[0];
    gl_Position = uViewProjection * gl_in[0].gl_Position;
    EmitVertex();
    gl_Position = uViewProjection * (gl_in[0].gl_Position + vec4(direction * uLength, 0.0));
    EmitVertex();
    EndPrimitive();
}
)";

// ---------------------------------------------------------------------------
// Infinite ground grid, raycast against y = 0 in the fragment shader so it
// stays crisp at every zoom level and needs no geometry.
// ---------------------------------------------------------------------------
inline constexpr const char* kGridVertex = R"(#version 330 core
out vec3 vNearPoint;
out vec3 vFarPoint;

uniform mat4 uInverseViewProjection;

vec3 unproject(vec2 ndc, float z) {
    vec4 point = uInverseViewProjection * vec4(ndc, z, 1.0);
    return point.xyz / point.w;
}

void main() {
    // Fullscreen triangle from gl_VertexID; no vertex buffer required.
    vec2 ndc = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNearPoint = unproject(ndc, -1.0);
    vFarPoint = unproject(ndc, 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

inline constexpr const char* kGridFragment = R"(#version 330 core
in vec3 vNearPoint;
in vec3 vFarPoint;

out vec4 fragColor;

uniform mat4  uViewProjection;
uniform vec3  uCameraPosition;
uniform vec3  uGridColor;
uniform float uCellSize;
uniform float uFadeDistance;

// Anti-aliased line mask for a grid of the given period.
float gridMask(vec2 position, float period) {
    vec2 coordinate = position / period;
    vec2 derivative = fwidth(coordinate);
    vec2 distanceToLine = abs(fract(coordinate - 0.5) - 0.5) / max(derivative, vec2(1e-8));
    return 1.0 - min(min(distanceToLine.x, distanceToLine.y), 1.0);
}

void main() {
    float t = -vNearPoint.y / (vFarPoint.y - vNearPoint.y);
    if (t < 0.0 || t > 1.0) discard;

    vec3 position = vNearPoint + t * (vFarPoint - vNearPoint);

    // Two decades of grid so the spacing stays readable while zooming.
    float minor = gridMask(position.xz, uCellSize);
    float major = gridMask(position.xz, uCellSize * 10.0);

    float distanceToCamera = length(position - uCameraPosition);
    float fade = 1.0 - clamp(distanceToCamera / uFadeDistance, 0.0, 1.0);
    fade *= fade;

    float strength = max(minor * 0.35, major * 0.75);
    if (strength <= 0.001 || fade <= 0.001) discard;

    vec3 color = uGridColor;
    // Tint the principal axes so orientation is readable at a glance.
    vec2 axisDistance = abs(position.xz) / max(fwidth(position.xz), vec2(1e-8));
    if (axisDistance.x < 1.0) color = vec3(0.85, 0.30, 0.35);
    else if (axisDistance.y < 1.0) color = vec3(0.35, 0.55, 0.90);

    vec4 clip = uViewProjection * vec4(position, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    fragColor = vec4(color, strength * fade);
}
)";

// ---------------------------------------------------------------------------
// Background gradient drawn as a fullscreen triangle.
// ---------------------------------------------------------------------------
inline constexpr const char* kBackgroundVertex = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 ndc = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vUv = ndc * 0.5 + 0.5;
    gl_Position = vec4(ndc, 1.0, 1.0);
}
)";

inline constexpr const char* kBackgroundFragment = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform vec3 uTop;
uniform vec3 uBottom;
void main() {
    vec3 color = mix(uBottom, uTop, smoothstep(0.0, 1.0, vUv.y));
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)";

}  // namespace tessera::gfx::shaders
