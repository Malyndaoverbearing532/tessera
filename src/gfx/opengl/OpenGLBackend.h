#pragma once

#include "gfx/RenderBackend.h"
#include "gfx/opengl/Framebuffer.h"
#include "gfx/opengl/GpuMesh.h"
#include "gfx/opengl/GpuTexture.h"
#include "gfx/opengl/Shader.h"

#include <string>
#include <vector>

namespace tessera::gfx {

/// Forward renderer on OpenGL 3.3 core - the reference backend, and the only
/// one that runs unmodified on macOS, Linux and Windows.
///
/// It owns the GPU mirror of the scene but never the scene itself, so loading a
/// new file is `setScene(...)` and nothing else.
class OpenGLBackend final : public IRenderBackend {
public:
    ~OpenGLBackend() override;

    [[nodiscard]] BackendInfo info() const override;
    [[nodiscard]] WindowRequirements windowRequirements(int samples) const override;

    [[nodiscard]] bool initialize(GLFWwindow* window, std::string& error) override;
    void shutdown() override;

    void setScene(const scene::Scene* scene) override;
    void render(const Camera& camera, const RenderSettings& settings, int width,
                int height) override;
    void present(GLFWwindow* window) override;

    [[nodiscard]] bool renderToImage(const Camera& camera, const RenderSettings& settings,
                                     int width, int height, int samples,
                                     std::vector<std::uint8_t>& rgba, std::string& error) override;

    [[nodiscard]] const FrameStats& stats() const override { return stats_; }

    void setMeshVisible(int meshIndex, bool visible) override;
    [[nodiscard]] bool meshVisible(int meshIndex) const override;
    void showAllMeshes() override;

    void setSelectedMesh(int meshIndex) override { selectedMesh_ = meshIndex; }
    [[nodiscard]] int selectedMesh() const override { return selectedMesh_; }

    std::vector<vec3>& measurePoints() override { return measurePoints_; }
    [[nodiscard]] const std::vector<vec3>& measurePoints() const override { return measurePoints_; }

    [[nodiscard]] bool supportsImGui() const override { return true; }

private:
    struct DrawItem {
        int meshIndex = -1;
        int material = -1;
        mat4 world{1.0f};
        /// Precomputed here rather than in the draw loop; inverting a matrix
        /// per mesh per frame is pure waste for a scene that is not moving.
        mat3 normalMatrix{1.0f};
        Aabb worldBounds;
        float viewDepth = 0.0f;
        bool blended = false;
        bool baked = false;    /// transform already in the vertex buffer
        bool culled = false;   /// recomputed every frame from the frustum
    };

    /// The pass itself, with no framebuffer binding of its own, so both the
    /// on-screen path and the offscreen capture can drive it.
    void renderScene(const Camera& camera, const RenderSettings& settings, int width, int height);

    /// Rebuilds the static half of the draw list: transforms, bounds and
    /// material grouping. Only scene or visibility changes invalidate it.
    void rebuildDrawList();
    /// Per-frame work: frustum culling, and depth sorting the blended items.
    void updateDrawList(const Camera& camera, int width, int height);
    void drawBackground(const RenderSettings& settings);
    void drawMeshes(const Camera& camera, const RenderSettings& settings, const mat4& viewProjection,
                    bool blendedPass);
    void drawGrid(const Camera& camera, const RenderSettings& settings, const mat4& viewProjection);
    void drawOverlays(const RenderSettings& settings, const mat4& viewProjection);
    void bindMaterial(const scene::Material& material, const RenderSettings& settings);
    void ensureLineGeometry();
    void uploadMeasureGeometry();

    const scene::Scene* scene_ = nullptr;
    std::vector<GpuMesh> gpuMeshes_;
    std::vector<GpuTexture> gpuTextures_;
    std::vector<bool> meshVisible_;
    /// True where the world transform is already folded into the vertex buffer.
    std::vector<bool> transformBaked_;
    std::vector<DrawItem> drawList_;
    bool drawListDirty_ = true;

    Shader meshShader_;
    Shader lineShader_;
    Shader normalsShader_;
    Shader gridShader_;
    Shader backgroundShader_;

    GpuTexture whiteTexture_;
    GpuTexture flatNormalTexture_;
    GpuTexture blackTexture_;

    GLuint emptyVao_ = 0;  ///< for attribute-less fullscreen passes
    GLuint boxVao_ = 0;    ///< unit cube edges, reused for every bounds draw
    GLuint boxVbo_ = 0;
    GLuint measureVao_ = 0;
    GLuint measureVbo_ = 0;
    std::size_t measureCapacity_ = 0;

    Aabb sceneBounds_;
    FrameStats stats_;

    /// Redundant-state tracking for the draw loop. Reset at the start of every
    /// pass, since another pass may have changed the binding behind our back.
    int boundMaterial_ = -2;
    int cullFaceEnabled_ = -1;  // -1 unknown, 0 disabled, 1 enabled

    int selectedMesh_ = -1;
    std::vector<vec3> measurePoints_;

    std::string deviceName_;
    bool initialized_ = false;
};

}  // namespace tessera::gfx
