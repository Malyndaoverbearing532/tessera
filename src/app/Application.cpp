#include "app/Application.h"

#include "app/PlatformIntegration.h"
#include "core/Log.h"
#include "io/Exporter.h"
#include "io/FileUtil.h"
#include "io/ImageLoader.h"
#include "io/ImporterRegistry.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numeric>

namespace tessera::app {
namespace {

/// Movement below this many pixels between press and release counts as a click
/// rather than a drag.
constexpr double kClickThreshold = 4.0;

constexpr float kOrbitSpeed = 0.008f;

void glfwErrorCallback(int code, const char* description) {
    log::error("glfw error {}: {}", code, description);
}

/// Translates a backend's stated needs into GLFW window hints. Backends that
/// manage their own surface (Vulkan, Metal) ask for no client API at all.
void applyWindowHints(const gfx::WindowRequirements& requirements) {
    glfwDefaultWindowHints();

    if (!requirements.needsOpenGLContext) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, requirements.contextMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, requirements.contextMinor);
    if (requirements.coreProfile) {
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }
    // macOS only exposes core profiles through the forward-compatible flag.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, requirements.forwardCompatible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, std::max(requirements.samples, 0));
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
}

std::string timestampedName(std::string_view prefix, std::string_view extension) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &time);
#else
    localtime_r(&time, &parts);
#endif
    return std::format("{}_{:04}{:02}{:02}_{:02}{:02}{:02}.{}", prefix, parts.tm_year + 1900,
                       parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec,
                       extension);
}

/// Renders offscreen and writes the result out. Shared by the screenshot action
/// and the headless mode, so both go through the identical backend path.
bool renderToPng(gfx::IRenderBackend& backend, const Camera& camera,
                 const gfx::RenderSettings& settings, int width, int height, int samples,
                 const std::filesystem::path& output, std::string& error) {
    std::vector<std::uint8_t> pixels;
    if (!backend.renderToImage(camera, settings, width, height, samples, pixels, error)) {
        return false;
    }
    // Backends return bottom-up rows, matching glReadPixels; the writer flips.
    return io::writePng(output, width, height, 4, pixels.data(), /*flipVertically=*/true, error);
}

}  // namespace

Application::~Application() { destroyWindow(); }

float Application::aspectRatio() const {
    return static_cast<float>(std::max(framebufferWidth_, 1)) /
           static_cast<float>(std::max(framebufferHeight_, 1));
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

bool Application::createWindow(const Options& options, const gfx::WindowRequirements& requirements,
                               std::string& error) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        error = "could not initialise GLFW";
        return false;
    }

    applyWindowHints(requirements);
    window_ = glfwCreateWindow(options.width, options.height, "tessera", nullptr, nullptr);
    if (!window_) {
        error = requirements.needsOpenGLContext
                    ? std::format("could not create a window with an OpenGL {}.{} core context",
                                  requirements.contextMajor, requirements.contextMinor)
                    : "could not create a window";
        glfwTerminate();
        return false;
    }

    glfwGetFramebufferSize(window_, &framebufferWidth_, &framebufferHeight_);
    installCallbacks();
    return true;
}

void Application::installCallbacks() {
    glfwSetWindowUserPointer(window_, this);

    const auto self = [](GLFWwindow* window) {
        return static_cast<Application*>(glfwGetWindowUserPointer(window));
    };

    glfwSetCursorPosCallback(window_, [](GLFWwindow* window, double x, double y) {
        static_cast<Application*>(glfwGetWindowUserPointer(window))->onCursorMove(x, y);
    });
    glfwSetMouseButtonCallback(window_, [](GLFWwindow* window, int button, int action, int mods) {
        static_cast<Application*>(glfwGetWindowUserPointer(window))->onMouseButton(button, action, mods);
    });
    glfwSetScrollCallback(window_, [](GLFWwindow* window, double x, double y) {
        static_cast<Application*>(glfwGetWindowUserPointer(window))->onScroll(x, y);
    });
    glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
        static_cast<Application*>(glfwGetWindowUserPointer(window))->onKey(key, scancode, action, mods);
    });
    glfwSetDropCallback(window_, [](GLFWwindow* window, int count, const char** paths) {
        static_cast<Application*>(glfwGetWindowUserPointer(window))->onDrop(count, paths);
    });
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* window, int width, int height) {
        static_cast<Application*>(glfwGetWindowUserPointer(window))->onFramebufferSize(width, height);
    });
    (void)self;
}

void Application::destroyWindow() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
    }
}

// ---------------------------------------------------------------------------
// Scene actions
// ---------------------------------------------------------------------------

bool Application::loadFile(const std::filesystem::path& path) {
    scene::Scene loaded;
    std::string error;
    if (!io::ImporterRegistry::instance().load(path, importOptions_, loaded, error)) {
        log::error("{}", error);
        status_ = std::format("Failed to open {}", path.filename().string());
        return false;
    }

    scene_ = std::move(loaded);
    currentPath_ = path;
    selectedNode_ = -1;
    selectedMesh_ = -1;

    renderer_->setScene(&scene_);
    picker_.setScene(&scene_);
    frameAll();

    if (window_) {
        const std::string title = std::format("tessera - {}", path.filename().string());
        glfwSetWindowTitle(window_, title.c_str());
    }

    status_ = std::format("{}  |  {} meshes, {} triangles  |  {} in {:.2f}s",
                          path.filename().string(), scene_.stats.meshCount,
                          scene_.stats.triangleCount, scene_.importerName, scene_.stats.loadSeconds);
    return true;
}

void Application::frameAll() {
    camera_.frame(scene_.bounds(), aspectRatio());
}

void Application::frameSelection() {
    if (selectedMesh_ < 0 || selectedMesh_ >= static_cast<int>(scene_.meshes.size())) {
        frameAll();
        return;
    }

    // Find the world transform of the first instance of the selected mesh.
    Aabb box;
    scene_.forEachMeshInstance([&](int, int meshIndex, const mat4& world) {
        if (meshIndex == selectedMesh_) {
            box.expand(scene_.meshes[static_cast<std::size_t>(meshIndex)].bounds.transformed(world));
        }
    });
    camera_.frame(box.valid() ? box : scene_.bounds(), aspectRatio());
}

void Application::saveScreenshot() {
    if (scene_.empty()) {
        status_ = "Nothing to capture.";
        return;
    }

    const std::filesystem::path output = timestampedName("tessera", "png");
    std::string error;
    if (renderToPng(*renderer_, camera_, settings_, framebufferWidth_, framebufferHeight_, 4, output,
                    error)) {
        status_ = std::format("Saved {}", output.string());
        log::info("{}", status_);
    } else {
        status_ = std::format("Screenshot failed: {}", error);
        log::error("{}", error);
    }
    // The next render() rebinds the window framebuffer itself, so the offscreen
    // target the capture left bound does not need undoing here.
}

void Application::exportScene(const std::filesystem::path& path, bool ascii) {
    if (scene_.empty()) {
        status_ = "Nothing to export.";
        return;
    }

    io::ExportOptions options;
    options.binary = !ascii;

    std::string error;
    if (io::ExporterRegistry::instance().save(scene_, path, options, error)) {
        status_ = std::format("Exported {}", path.filename().string());
    } else {
        status_ = std::format("Export failed: {}", error);
        log::error("{}", error);
    }
}

void Application::toggleFullscreen() {
    if (!window_) return;

    if (fullscreen_) {
        glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedWidth_,
                             windowedHeight_, 0);
        fullscreen_ = false;
        return;
    }

    glfwGetWindowPos(window_, &windowedX_, &windowedY_);
    glfwGetWindowSize(window_, &windowedWidth_, &windowedHeight_);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return;

    glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    fullscreen_ = true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Application::onFramebufferSize(int width, int height) {
    framebufferWidth_ = std::max(width, 1);
    framebufferHeight_ = std::max(height, 1);
}

void Application::onCursorMove(double x, double y) {
    const double dx = x - lastCursorX_;
    const double dy = y - lastCursorY_;
    lastCursorX_ = x;
    lastCursorY_ = y;

    if (!dragging_) return;
    if (std::abs(x - pressCursorX_) > kClickThreshold ||
        std::abs(y - pressCursorY_) > kClickThreshold) {
        dragMoved_ = true;
    }

    const bool shift = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                       glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    const bool panning = dragButton_ == GLFW_MOUSE_BUTTON_RIGHT || shift;

    if (panning) {
        camera_.pan(static_cast<float>(dx), static_cast<float>(dy), aspectRatio(),
                    framebufferHeight_);
    } else {
        camera_.orbit(static_cast<float>(-dx) * kOrbitSpeed, static_cast<float>(-dy) * kOrbitSpeed);
    }
}

void Application::onMouseButton(int button, int action, int mods) {
    if (ui_.wantsMouse() && action == GLFW_PRESS) return;

    if (action == GLFW_PRESS) {
        glfwGetCursorPos(window_, &pressCursorX_, &pressCursorY_);
        lastCursorX_ = pressCursorX_;
        lastCursorY_ = pressCursorY_;
        dragging_ = true;
        dragMoved_ = false;
        dragButton_ = button;
        return;
    }

    if (action == GLFW_RELEASE && dragging_) {
        dragging_ = false;
        // A press-release without meaningful movement is a click, not an orbit.
        if (!dragMoved_ && button == GLFW_MOUSE_BUTTON_LEFT && !ui_.wantsMouse()) {
            handleViewportClick(pressCursorX_, pressCursorY_);
        }
    }
    (void)mods;
}

void Application::onScroll(double xOffset, double yOffset) {
    if (ui_.wantsMouse()) return;
    camera_.dolly(static_cast<float>(yOffset));
    camera_.updateClipPlanes(scene_.bounds());
    (void)xOffset;
}

void Application::handleViewportClick(double x, double y) {
    if (scene_.empty()) return;

    // Cursor coordinates are in window space; the framebuffer may be larger on
    // a HiDPI display, so scale before converting to NDC.
    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(window_, &windowWidth, &windowHeight);
    if (windowWidth <= 0 || windowHeight <= 0) return;

    const vec2 ndc(static_cast<float>(x / windowWidth) * 2.0f - 1.0f,
                   1.0f - static_cast<float>(y / windowHeight) * 2.0f);
    const Ray ray = camera_.rayThrough(ndc, aspectRatio());

    std::vector<bool> visible(scene_.meshes.size(), true);
    for (std::size_t i = 0; i < visible.size(); ++i) {
        visible[i] = renderer_->meshVisible(static_cast<int>(i));
    }

    const auto hit = picker_.pick(ray, visible);
    if (!hit) {
        if (!measureMode_) {
            selectedMesh_ = -1;
            selectedNode_ = -1;
            renderer_->setSelectedMesh(-1);
            status_ = "Nothing under the cursor.";
        }
        return;
    }

    if (measureMode_) {
        renderer_->measurePoints().push_back(hit->position);
        const std::size_t count = renderer_->measurePoints().size();
        if (count >= 2 && count % 2 == 0) {
            const float length =
                glm::length(renderer_->measurePoints()[count - 1] - renderer_->measurePoints()[count - 2]);
            status_ = std::format("Distance: {:.6g}", static_cast<double>(length));
        } else {
            status_ = std::format("Point at {:.4g} {:.4g} {:.4g}", hit->position.x, hit->position.y,
                                  hit->position.z);
        }
        return;
    }

    selectedMesh_ = hit->meshIndex;
    selectedNode_ = hit->nodeIndex;
    renderer_->setSelectedMesh(hit->meshIndex);
    status_ = std::format("{}  |  triangle {}  |  {:.4g} {:.4g} {:.4g}",
                          scene_.meshes[static_cast<std::size_t>(hit->meshIndex)].name,
                          hit->triangleIndex, hit->position.x, hit->position.y, hit->position.z);
}

void Application::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (ui_.wantsKeyboard()) return;

    const bool control = (mods & GLFW_MOD_CONTROL) != 0 || (mods & GLFW_MOD_SUPER) != 0;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    const bool alt = (mods & GLFW_MOD_ALT) != 0;

    if (control) {
        switch (key) {
            case GLFW_KEY_O: ui_.openFileBrowser(); return;
            case GLFW_KEY_E: ui_.openExportDialog(); return;
            case GLFW_KEY_Q: glfwSetWindowShouldClose(window_, GLFW_TRUE); return;
            case GLFW_KEY_R:
                if (!currentPath_.empty()) loadFile(currentPath_);
                return;
            case GLFW_KEY_1: camera_.setView(Camera::View::Back); return;
            case GLFW_KEY_3: camera_.setView(Camera::View::Left); return;
            case GLFW_KEY_7: camera_.setView(Camera::View::Bottom); return;
            default: break;
        }
    }

    switch (key) {
        case GLFW_KEY_ESCAPE:
            selectedMesh_ = -1;
            selectedNode_ = -1;
            renderer_->setSelectedMesh(-1);
            break;
        case GLFW_KEY_F:
            if (shift) frameSelection();
            else frameAll();
            break;
        case GLFW_KEY_W: settings_.wireframe = !settings_.wireframe; break;
        case GLFW_KEY_N: settings_.showNormals = !settings_.showNormals; break;
        case GLFW_KEY_B: settings_.showBounds = !settings_.showBounds; break;
        case GLFW_KEY_G: settings_.showGrid = !settings_.showGrid; break;
        case GLFW_KEY_T: settings_.useTextures = !settings_.useTextures; break;
        case GLFW_KEY_C: {
            const int next = (static_cast<int>(settings_.shading) + 1) %
                             static_cast<int>(gfx::ShadingMode::Count);
            settings_.shading = static_cast<gfx::ShadingMode>(next);
            status_ = std::format("Shading: {}", gfx::shadingModeName(settings_.shading));
            break;
        }
        case GLFW_KEY_M:
            measureMode_ = !measureMode_;
            status_ = measureMode_ ? "Measure mode: click the model to drop points."
                                   : "Measure mode off.";
            break;
        case GLFW_KEY_H:
            if (alt) {
                renderer_->showAllMeshes();
                status_ = "All meshes shown.";
            } else if (selectedMesh_ >= 0) {
                renderer_->setMeshVisible(selectedMesh_, false);
                status_ = "Mesh hidden (Alt+H shows everything again).";
            }
            break;
        case GLFW_KEY_SPACE: turntable_ = !turntable_; break;
        case GLFW_KEY_TAB: showUi_ = !showUi_; break;
        case GLFW_KEY_F11: toggleFullscreen(); break;
        case GLFW_KEY_F12: saveScreenshot(); break;
        case GLFW_KEY_1: camera_.setView(Camera::View::Front); break;
        case GLFW_KEY_3: camera_.setView(Camera::View::Right); break;
        case GLFW_KEY_7: camera_.setView(Camera::View::Top); break;
        case GLFW_KEY_0: camera_.setView(Camera::View::Isometric); break;
        case GLFW_KEY_5:
            camera_.projectionMode = camera_.projectionMode == Camera::Projection::Perspective
                                         ? Camera::Projection::Orthographic
                                         : Camera::Projection::Perspective;
            break;
        default: break;
    }
}

void Application::onDrop(int count, const char** paths) {
    if (count <= 0 || paths == nullptr) return;
    // Only the first file is opened; the viewer shows one scene at a time.
    loadFile(paths[0]);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

int Application::run(const Options& options) {
    io::registerBuiltinImporters();
    io::registerBuiltinExporters();

    settings_ = options.render;
    importOptions_ = options.import;

    // Backend first: it decides what kind of window it needs.
    std::string error;
    const std::string backendKey =
        options.backend.empty() ? gfx::BackendRegistry::instance().defaultKey() : options.backend;

    renderer_ = gfx::BackendRegistry::instance().create(backendKey, error);
    if (!renderer_) {
        log::error("{}", error);
        return 1;
    }

    if (!createWindow(options, renderer_->windowRequirements(options.samples), error)) {
        log::error("{}", error);
        return 1;
    }

    if (!renderer_->initialize(window_, error)) {
        log::error("renderer: {}", error);
        destroyWindow();
        return 1;
    }

    // Dear ImGui renders through a backend of its own that must match the
    // graphics API. Where there is no match the viewer runs keyboard-only
    // rather than drawing a broken interface.
    if (renderer_->supportsImGui()) {
        if (!ui_.initialize(window_, error)) {
            log::error("ui: {}", error);
            destroyWindow();
            return 1;
        }
    } else {
        log::warn("the {} backend has no Dear ImGui integration; panels are disabled",
                  renderer_->info().displayName);
        showUi_ = false;
    }

    // Files opened from the desktop shell arrive asynchronously rather than in
    // argv, so start listening before the first poll delivers them.
    platform::installOpenFileHandler();

    if (!options.input.empty()) loadFile(options.input);

    double previousTime = glfwGetTime();
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        // Drain outside the event callback, so loading never runs while the
        // platform is midway through dispatching.
        for (std::string requested; platform::takePendingOpenFile(requested);) {
            loadFile(requested);
        }

        const double now = glfwGetTime();
        const double delta = std::clamp(now - previousTime, 0.0, 0.25);
        previousTime = now;
        frameMilliseconds_ = delta * 1000.0;
        // Smoothed so the readout is legible rather than jittering every frame.
        framesPerSecond_ = framesPerSecond_ * 0.9 + (delta > 0.0 ? 0.1 / delta : 0.0);

        if (turntable_) camera_.orbit(static_cast<float>(delta) * 0.4f, 0.0f);

        ui_.beginFrame();

        ui::UiContext context;
        context.scene = scene_.empty() ? nullptr : &scene_;
        context.renderer = renderer_.get();
        context.settings = &settings_;
        context.camera = &camera_;
        context.importOptions = &importOptions_;
        context.showUi = &showUi_;
        context.turntable = &turntable_;
        context.measureMode = &measureMode_;
        context.selectedNode = &selectedNode_;
        context.selectedMesh = &selectedMesh_;
        context.statusMessage = status_;
        context.frameMilliseconds = frameMilliseconds_;
        context.framesPerSecond = framesPerSecond_;
        context.openFile = [this](const std::filesystem::path& path) { loadFile(path); };
        context.exportFile = [this](const std::filesystem::path& path, bool ascii) {
            exportScene(path, ascii);
        };
        context.takeScreenshot = [this] { saveScreenshot(); };
        context.frameAll = [this] { frameAll(); };
        context.frameSelection = [this] { frameSelection(); };
        context.reload = [this] {
            if (!currentPath_.empty()) loadFile(currentPath_);
        };
        context.quit = [this] { glfwSetWindowShouldClose(window_, GLFW_TRUE); };

        ui_.draw(context);

        renderer_->render(camera_, settings_, framebufferWidth_, framebufferHeight_);

        ui_.endFrame();
        renderer_->present(window_);
    }

    ui_.shutdown();
    renderer_->shutdown();
    // Release the backend entirely before GLFW tears the context down, so no
    // GL object outlives the context it belongs to.
    renderer_.reset();
    destroyWindow();
    return 0;
}

// ---------------------------------------------------------------------------
// Non-interactive modes
// ---------------------------------------------------------------------------

int Application::runConvert(const Options& options) {
    io::registerBuiltinImporters();
    io::registerBuiltinExporters();

    scene::Scene scene;
    std::string error;
    if (!io::ImporterRegistry::instance().load(options.input, options.import, scene, error)) {
        log::error("{}", error);
        return 1;
    }

    if (!io::ExporterRegistry::instance().save(scene, options.convertOutput, options.exportOptions,
                                               error)) {
        log::error("{}", error);
        return 1;
    }
    return 0;
}

int Application::runBenchmark(const Options& options) {
    io::registerBuiltinImporters();

    scene::Scene scene;
    std::string error;
    if (!io::ImporterRegistry::instance().load(options.input, options.import, scene, error)) {
        log::error("{}", error);
        return 1;
    }

    const std::string backendKey =
        options.backend.empty() ? gfx::BackendRegistry::instance().defaultKey() : options.backend;
    gfx::BackendPtr renderer = gfx::BackendRegistry::instance().create(backendKey, error);
    if (!renderer) {
        log::error("{}", error);
        return 1;
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        log::error("could not initialise GLFW");
        return 1;
    }

    applyWindowHints(renderer->windowRequirements(options.samples));
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(options.width, options.height, "tessera", nullptr, nullptr);
    if (!window) {
        log::error("could not create an offscreen rendering context");
        glfwTerminate();
        return 1;
    }

    int exitCode = 0;
    if (!renderer->initialize(window, error)) {
        log::error("renderer: {}", error);
        exitCode = 1;
    } else {
        // Vsync would cap every measurement at the refresh rate and tell us
        // nothing about the renderer.
        glfwSwapInterval(0);
        renderer->setScene(&scene);

        Camera camera;
        camera.setView(Camera::View::Isometric);
        const float aspect =
            static_cast<float>(options.width) / static_cast<float>(std::max(options.height, 1));
        camera.frame(scene.bounds(), aspect);

        const int frames = std::max(options.benchmarkFrames, 1);
        // Discard the first frames: shader compilation, buffer residency and
        // driver warm-up all land there and would skew the mean.
        const int warmup = std::min(20, std::max(1, frames / 10));

        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(frames));

        for (int i = 0; i < warmup + frames; ++i) {
            // Rotating the camera keeps the driver from caching a static frame
            // and makes the numbers representative of actual interaction.
            camera.orbit(0.01f, 0.0f);

            const auto start = std::chrono::steady_clock::now();
            renderer->render(camera, options.render, options.width, options.height);
            renderer->present(window);
            const auto end = std::chrono::steady_clock::now();

            if (i >= warmup) {
                samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
            }
        }

        std::sort(samples.begin(), samples.end());
        const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
        const double mean = total / static_cast<double>(samples.size());
        const double median = samples[samples.size() / 2];
        const double p95 = samples[static_cast<std::size_t>(
            std::min<double>(static_cast<double>(samples.size()) - 1,
                             static_cast<double>(samples.size()) * 0.95))];

        const gfx::FrameStats& frameStats = renderer->stats();
        std::printf(
            "%-22s %s\n"
            "  scene        %zu meshes, %zu triangles, %zu materials\n"
            "  per frame    %d draw calls, %zu triangles submitted\n"
            "  frames       %zu measured, %d discarded as warm-up, %dx%d\n"
            "  mean         %8.3f ms   (%.1f fps)\n"
            "  median       %8.3f ms   (%.1f fps)\n"
            "  best         %8.3f ms\n"
            "  95th pct     %8.3f ms\n",
            "benchmark", options.input.filename().string().c_str(), scene.stats.meshCount,
            scene.stats.triangleCount, scene.stats.materialCount, frameStats.drawCalls,
            frameStats.trianglesDrawn, samples.size(), warmup, options.width, options.height,
            mean, 1000.0 / mean, median, 1000.0 / median, samples.front(), p95);

        renderer->shutdown();
    }

    renderer.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}

int Application::runHeadlessRender(const Options& options) {
    io::registerBuiltinImporters();

    scene::Scene scene;
    std::string error;
    if (!io::ImporterRegistry::instance().load(options.input, options.import, scene, error)) {
        log::error("{}", error);
        return 1;
    }

    const std::string backendKey =
        options.backend.empty() ? gfx::BackendRegistry::instance().defaultKey() : options.backend;
    gfx::BackendPtr renderer = gfx::BackendRegistry::instance().create(backendKey, error);
    if (!renderer) {
        log::error("{}", error);
        return 1;
    }

    // Rendering needs a device, but not a visible window.
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        log::error("could not initialise GLFW");
        return 1;
    }

    applyWindowHints(renderer->windowRequirements(options.samples));
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(std::max(options.width, 1), std::max(options.height, 1),
                                          "tessera", nullptr, nullptr);
    if (!window) {
        log::error("could not create an offscreen rendering context");
        glfwTerminate();
        return 1;
    }

    int exitCode = 0;
    {
        if (!renderer->initialize(window, error)) {
            log::error("renderer: {}", error);
            exitCode = 1;
        } else {
            renderer->setScene(&scene);

            Camera camera;
            camera.setView(Camera::View::Isometric);
            const float aspect =
                static_cast<float>(options.width) / static_cast<float>(std::max(options.height, 1));
            camera.frame(scene.bounds(), aspect);

            if (!renderToPng(*renderer, camera, options.render, options.width, options.height,
                             options.samples, options.renderOutput, error)) {
                log::error("{}", error);
                exitCode = 1;
            } else {
                log::info("wrote {} ({}x{})", options.renderOutput.string(), options.width,
                          options.height);
            }
            renderer->shutdown();
        }
    }

    // Same ordering rule as the interactive path: the backend must be gone
    // before the context is.
    renderer.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}

}  // namespace tessera::app
