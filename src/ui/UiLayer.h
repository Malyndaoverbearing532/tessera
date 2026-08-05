#pragma once

#include "camera/Camera.h"
#include "gfx/RenderSettings.h"
#include "gfx/RenderBackend.h"
#include "io/Importer.h"
#include "scene/Scene.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace tessera::ui {

/// Everything the panels are allowed to read or change, plus the actions they
/// can trigger. Passing this instead of the Application keeps the UI free of
/// any dependency on windowing or the main loop.
struct UiContext {
    const scene::Scene* scene = nullptr;
    gfx::IRenderBackend* renderer = nullptr;
    gfx::RenderSettings* settings = nullptr;
    Camera* camera = nullptr;
    io::ImportOptions* importOptions = nullptr;

    // Live state owned by the application.
    bool* showUi = nullptr;
    bool* turntable = nullptr;
    bool* measureMode = nullptr;
    int* selectedNode = nullptr;
    int* selectedMesh = nullptr;
    std::string statusMessage;
    double frameMilliseconds = 0.0;
    double framesPerSecond = 0.0;

    // Actions.
    std::function<void(const std::filesystem::path&)> openFile;
    std::function<void(const std::filesystem::path&, bool ascii)> exportFile;
    std::function<void()> takeScreenshot;
    std::function<void()> frameAll;
    std::function<void()> frameSelection;
    std::function<void()> reload;
    std::function<void()> quit;
};

/// Dear ImGui front end. Compiled out entirely when TESSERA_WITH_UI is off, in
/// which case the viewer still runs with keyboard controls only.
class UiLayer {
public:
    bool initialize(GLFWwindow* window, std::string& error);
    void shutdown();

    void beginFrame();
    void draw(UiContext& context);
    void endFrame();

    /// True when ImGui wants the event, so the viewport should ignore it.
    [[nodiscard]] bool wantsMouse() const;
    [[nodiscard]] bool wantsKeyboard() const;

    void openFileBrowser();
    void openExportDialog();

private:
    void drawMenuBar(UiContext& context);
    void drawOutliner(UiContext& context);
    void drawInspector(UiContext& context);
    void drawRenderSettings(UiContext& context);
    void drawStats(UiContext& context);
    void drawMeasurePanel(UiContext& context);
    void drawFileBrowser(UiContext& context);
    void drawExportDialog(UiContext& context);
    void drawStatusBar(UiContext& context);
    void drawShortcuts();

    bool initialized_ = false;
    bool showBrowser_ = false;
    bool showExport_ = false;
    bool showShortcuts_ = false;
    bool showAbout_ = false;
    bool showDemo_ = false;

    std::filesystem::path browserDirectory_;
    std::string browserSelection_;
    std::string exportPath_;
    int exportFormatIndex_ = 0;
    bool exportAscii_ = false;

    // Rolling frame-time history for the stats graph.
    std::vector<float> frameTimes_;
    std::size_t frameTimeCursor_ = 0;
};

}  // namespace tessera::ui
