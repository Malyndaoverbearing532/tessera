#pragma once

#include "camera/Camera.h"
#include "gfx/RenderSettings.h"
#include "scene/Scene.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace tessera::gfx {

/// Graphics APIs the viewer knows about. A backend may be known but not
/// compiled in, and compiled in but unusable on the current machine; those are
/// three different states and the UI reports all of them.
enum class BackendId { OpenGL, Vulkan, Metal, Optix, Cuda, Count };

struct BackendInfo {
    BackendId id = BackendId::OpenGL;
    std::string key;          ///< command-line name, e.g. "opengl"
    std::string displayName;  ///< e.g. "OpenGL 3.3 core"
    bool compiledIn = false;  ///< present in this build
    bool available = false;   ///< usable right now on this machine
    std::string status;       ///< device name when available, reason when not
};

/// What the backend needs from the window before it exists. GLFW has to be told
/// up front whether to create a GL context at all, so this is queried from a
/// freshly constructed backend and applied as window hints.
struct WindowRequirements {
    bool needsOpenGLContext = true;
    int contextMajor = 3;
    int contextMinor = 3;
    bool coreProfile = true;
    bool forwardCompatible = true;
    int samples = 4;
};

/// The rendering contract. Everything above this line - importers, the scene
/// representation, the camera, picking - is API-agnostic, so a new backend
/// means implementing this interface and nothing else.
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    [[nodiscard]] virtual BackendInfo info() const = 0;
    [[nodiscard]] virtual WindowRequirements windowRequirements(int samples) const = 0;

    /// Brings up device resources. `window` is null for headless rendering.
    [[nodiscard]] virtual bool initialize(GLFWwindow* window, std::string& error) = 0;
    virtual void shutdown() = 0;

    /// Uploads GPU resources for `scene`; null releases them.
    virtual void setScene(const scene::Scene* scene) = 0;

    virtual void render(const Camera& camera, const RenderSettings& settings, int width,
                        int height) = 0;

    /// Presents the last frame. Separate from render() so the UI layer can draw
    /// in between.
    virtual void present(GLFWwindow* window) = 0;

    /// Renders offscreen and returns tightly packed RGBA8, bottom row first.
    [[nodiscard]] virtual bool renderToImage(const Camera& camera, const RenderSettings& settings,
                                             int width, int height, int samples,
                                             std::vector<std::uint8_t>& rgba,
                                             std::string& error) = 0;

    [[nodiscard]] virtual const FrameStats& stats() const = 0;

    // Per-mesh visibility, driven by the outliner.
    virtual void setMeshVisible(int meshIndex, bool visible) = 0;
    [[nodiscard]] virtual bool meshVisible(int meshIndex) const = 0;
    virtual void showAllMeshes() = 0;

    virtual void setSelectedMesh(int meshIndex) = 0;
    [[nodiscard]] virtual int selectedMesh() const = 0;

    virtual std::vector<vec3>& measurePoints() = 0;
    [[nodiscard]] virtual const std::vector<vec3>& measurePoints() const = 0;

    /// Whether Dear ImGui can render through this backend. The ImGui render
    /// backend has to match the graphics API, so a backend without one runs the
    /// viewer in keyboard-only mode instead of drawing a broken UI.
    [[nodiscard]] virtual bool supportsImGui() const { return false; }
};

using BackendPtr = std::unique_ptr<IRenderBackend>;

/// Creates backends by name and reports what this build supports.
class BackendRegistry {
public:
    static BackendRegistry& instance();

    /// Every known backend, including ones not compiled in, in preference order.
    [[nodiscard]] std::vector<BackendInfo> all() const;

    /// Backend that would be chosen when none is named.
    [[nodiscard]] std::string defaultKey() const;

    /// Constructs a backend. Returns null and fills `error` when the name is
    /// unknown or the backend is not part of this build.
    [[nodiscard]] BackendPtr create(std::string_view key, std::string& error) const;
};

}  // namespace tessera::gfx
