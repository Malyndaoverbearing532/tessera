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

/// The rendering contract. Everything above this line, importers, the scene
/// representation, the camera and picking, is API-agnostic, so a new backend
/// means implementing this interface and nothing else.
///
/// Two rules apply to every method and are not repeated below:
///
///   Threading. Every call comes from the thread that created the window.
///   Backends may assume that and there is no locking. Internal worker threads
///   are fine provided they are synchronised before the method returns.
///
///   Lifetime. Device objects must not outlive the context that owns them.
///   See shutdown().
///
/// docs/BACKENDS.md carries the full semantics, including the failure-reporting
/// taxonomy and the swapchain recreation rules.
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    /// Describes the backend and whether a usable device exists right now.
    /// Callable before initialize(), cheap, and must leave nothing behind: it
    /// may create a throwaway instance to enumerate devices but must destroy
    /// it. `--list-backends` calls this on every backend.
    [[nodiscard]] virtual BackendInfo info() const = 0;
    [[nodiscard]] virtual WindowRequirements windowRequirements(int samples) const = 0;

    /// Brings up every device resource: instance or context, device, surface,
    /// swapchain, render targets, pipelines. `window` is null when headless.
    ///
    /// Returning false means the hardware is present but could not be brought
    /// up. Hardware being absent entirely is reported through info() instead,
    /// before this is ever called. Not called twice without a shutdown().
    [[nodiscard]] virtual bool initialize(GLFWwindow* window, std::string& error) = 0;

    /// Releases every device object **while the context is still current**.
    /// Anything left to a member destructor runs after the window is gone,
    /// which macOS quietly tolerates and other drivers turn into a crash.
    virtual void shutdown() = 0;

    /// Uploads GPU resources for `scene`; null releases them. Safe to call
    /// repeatedly, never called during render().
    virtual void setScene(const scene::Scene* scene) = 0;

    /// Draws one frame at the given size.
    ///
    /// The size is passed every call and may change between any two of them;
    /// there is deliberately no resize(). Rebuild internally when it differs
    /// from what you built. Dropping a frame to do so is acceptable, failing
    /// is not.
    virtual void render(const Camera& camera, const RenderSettings& settings, int width,
                        int height) = 0;

    /// Presents the last frame. Separate from render() so the UI layer can draw
    /// in between. `window` is the one given to initialize(). May also have to
    /// rebuild the swapchain, since some APIs only report it as out of date at
    /// present time.
    virtual void present(GLFWwindow* window) = 0;

    /// Whether the backend can still draw.
    ///
    /// render() and present() return void deliberately: a frame that fails to
    /// draw is the backend's problem to absorb, and a caller cannot do anything
    /// useful with a per-frame error code. Losing the device is different. It
    /// is permanent, every later frame fails identically, and continuing to
    /// spin producing nothing is the worst possible behaviour because the user
    /// sees a frozen window with no explanation.
    ///
    /// So: absorb frame-level failures silently, and latch this to false when
    /// the device is gone. Once false it stays false. The application checks
    /// after each frame and exits with the reason rather than looping.
    [[nodiscard]] virtual bool operational() const { return true; }

    /// Why drawing stopped. Meaningful once operational() is false, and should
    /// name the actual cause, for example "device lost" or "out of device
    /// memory", not "render failed".
    [[nodiscard]] virtual std::string failureReason() const { return {}; }

    /// Renders offscreen into `rgba`: tightly packed RGBA8, **bottom row
    /// first**, matching glReadPixels. Backends whose images come out top-down
    /// must flip before returning, or every thumbnail is upside down.
    ///
    /// **Synchronous.** On returning true the data is complete and no GPU work
    /// is outstanding; wait before returning. Callers write a PNG on the next
    /// line and the process may then exit. Must also work with no window,
    /// since this is the only drawing path headless mode uses.
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
