#pragma once

#include "app/CommandLine.h"
#include "camera/Camera.h"
#include "gfx/RenderBackend.h"
#include "scene/Scene.h"
#include "tools/Picker.h"
#include "ui/UiLayer.h"

#include <filesystem>
#include <string>

struct GLFWwindow;

namespace tessera::app {

/// Window, input and main loop. Also hosts the two non-interactive modes so
/// they share exactly the same import and render paths as the viewer.
class Application {
public:
    Application() = default;
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Opens the interactive viewer. Returns a process exit code.
    int run(const Options& options);

    /// Reads a file and writes it back out in another format. No GL context.
    static int runConvert(const Options& options);

    /// Renders a PNG using a hidden window, then exits.
    static int runHeadlessRender(const Options& options);

    /// Renders repeatedly into a hidden window and reports frame timings.
    /// Exists so renderer changes can be measured rather than guessed at.
    static int runBenchmark(const Options& options);

private:
    bool createWindow(const Options& options, const gfx::WindowRequirements& requirements,
                      std::string& error);
    void installCallbacks();
    void destroyWindow();

    bool loadFile(const std::filesystem::path& path);
    void saveScreenshot();
    void exportScene(const std::filesystem::path& path, bool ascii);
    void frameAll();
    void frameSelection();
    void toggleFullscreen();

    void onCursorMove(double x, double y);
    void onMouseButton(int button, int action, int mods);
    void onScroll(double xOffset, double yOffset);
    void onKey(int key, int scancode, int action, int mods);
    void onDrop(int count, const char** paths);
    void onFramebufferSize(int width, int height);

    /// Turns a click into a selection or a measurement point.
    void handleViewportClick(double x, double y);

    [[nodiscard]] float aspectRatio() const;

    GLFWwindow* window_ = nullptr;
    int framebufferWidth_ = 1280;
    int framebufferHeight_ = 800;

    scene::Scene scene_;
    /// Chosen at startup from --backend; owns every graphics-API resource.
    gfx::BackendPtr renderer_;
    gfx::RenderSettings settings_;
    Camera camera_;
    tools::Picker picker_;
    ui::UiLayer ui_;
    io::ImportOptions importOptions_;

    std::filesystem::path currentPath_;
    std::string status_ = "Ready. Drop a model on the window, or press Ctrl+O.";

    // Interaction state
    bool dragging_ = false;
    int dragButton_ = -1;
    bool dragMoved_ = false;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
    double pressCursorX_ = 0.0;
    double pressCursorY_ = 0.0;

    bool showUi_ = true;
    bool turntable_ = false;
    bool measureMode_ = false;
    bool fullscreen_ = false;
    int selectedNode_ = -1;
    int selectedMesh_ = -1;

    // Saved window placement, restored when leaving fullscreen.
    int windowedX_ = 0;
    int windowedY_ = 0;
    int windowedWidth_ = 1280;
    int windowedHeight_ = 800;

    double framesPerSecond_ = 0.0;
    double frameMilliseconds_ = 0.0;
};

}  // namespace tessera::app
