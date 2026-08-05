#include "ui/UiLayer.h"

#if defined(TESSERA_WITH_UI)

#include "core/Log.h"
#include "io/Exporter.h"
#include "io/FileUtil.h"
#include "io/ImporterRegistry.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <format>

namespace tessera::ui {
namespace {

constexpr std::size_t kFrameHistory = 120;

void applyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FramePadding = ImVec2(7, 4);
    style.ItemSpacing = ImVec2(8, 5);
    style.WindowTitleAlign = ImVec2(0.02f, 0.5f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.11f, 0.96f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.28f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.31f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.38f, 0.46f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.31f, 0.38f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.60f, 0.25f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.52f, 0.22f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.26f, 0.30f, 1.00f);
}

/// glm <-> ImGui colour widgets.
bool colorEdit(const char* label, vec3& color) {
    float values[3] = {color.r, color.g, color.b};
    if (!ImGui::ColorEdit3(label, values, ImGuiColorEditFlags_NoInputs)) return false;
    color = vec3(values[0], values[1], values[2]);
    return true;
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

std::string humanCount(std::size_t value) {
    if (value < 1000) return std::format("{}", value);
    if (value < 1000000) return std::format("{:.1f}K", static_cast<double>(value) / 1e3);
    return std::format("{:.2f}M", static_cast<double>(value) / 1e6);
}

}  // namespace

bool UiLayer::initialize(GLFWwindow* window, std::string& error) {
    IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr) {
        error = "could not create the ImGui context";
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no stray imgui.ini next to the binary
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    applyTheme();

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        error = "ImGui GLFW backend failed to start";
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        error = "ImGui OpenGL backend failed to start";
        return false;
    }

    frameTimes_.assign(kFrameHistory, 0.0f);
    browserDirectory_ = std::filesystem::current_path();
    initialized_ = true;
    return true;
}

void UiLayer::shutdown() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void UiLayer::beginFrame() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void UiLayer::endFrame() {
    if (!initialized_) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool UiLayer::wantsMouse() const {
    return initialized_ && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
}

bool UiLayer::wantsKeyboard() const {
    return initialized_ && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
}

void UiLayer::openFileBrowser() { showBrowser_ = true; }
void UiLayer::openExportDialog() { showExport_ = true; }

void UiLayer::draw(UiContext& context) {
    if (!initialized_) return;

    frameTimes_[frameTimeCursor_] = static_cast<float>(context.frameMilliseconds);
    frameTimeCursor_ = (frameTimeCursor_ + 1) % frameTimes_.size();

    drawMenuBar(context);

    // The browser and export dialog stay available even with the panels hidden,
    // otherwise a keyboard shortcut could open a window nobody can see.
    if (showBrowser_) drawFileBrowser(context);
    if (showExport_) drawExportDialog(context);

    if (!context.showUi || !*context.showUi) {
        drawStatusBar(context);
        return;
    }

    drawOutliner(context);
    drawInspector(context);
    drawRenderSettings(context);
    drawStats(context);
    if (context.measureMode && *context.measureMode) drawMeasurePanel(context);
    if (showShortcuts_) drawShortcuts();
    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
    drawStatusBar(context);

    if (showAbout_) {
        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
        if (ImGui::Begin("About tessera", &showAbout_, ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextWrapped(
                "tessera is a small, modular 3D model viewer.\n\n"
                "Native fast-path readers handle OBJ, STL and PLY; everything else "
                "goes through Assimp, so roughly fifty formats open with no extra work.");
            ImGui::Separator();
            ImGui::Text("%zu readable formats",
                        io::ImporterRegistry::instance().supportedFormats().size());
            ImGui::Text("%zu writable formats",
                        io::ExporterRegistry::instance().supportedFormats().size());
        }
        ImGui::End();
    }
}

void UiLayer::drawMenuBar(UiContext& context) {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O")) showBrowser_ = true;
        if (ImGui::MenuItem("Reload", "Ctrl+R", false, context.scene != nullptr)) {
            if (context.reload) context.reload();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export...", "Ctrl+E", false, context.scene != nullptr)) {
            showExport_ = true;
        }
        if (ImGui::MenuItem("Screenshot", "F12", false, context.scene != nullptr)) {
            if (context.takeScreenshot) context.takeScreenshot();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            if (context.quit) context.quit();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Frame all", "F")) {
            if (context.frameAll) context.frameAll();
        }
        if (ImGui::MenuItem("Frame selection", "Shift+F", false,
                            context.selectedMesh && *context.selectedMesh >= 0)) {
            if (context.frameSelection) context.frameSelection();
        }
        ImGui::Separator();

        if (context.camera) {
            const std::pair<const char*, Camera::View> presets[] = {
                {"Front  (1)", Camera::View::Front},   {"Back   (Ctrl+1)", Camera::View::Back},
                {"Right  (3)", Camera::View::Right},   {"Left   (Ctrl+3)", Camera::View::Left},
                {"Top    (7)", Camera::View::Top},     {"Bottom (Ctrl+7)", Camera::View::Bottom},
                {"Iso    (0)", Camera::View::Isometric},
            };
            for (const auto& [label, preset] : presets) {
                if (ImGui::MenuItem(label)) context.camera->setView(preset);
            }
            ImGui::Separator();
            bool orthographic = context.camera->projectionMode == Camera::Projection::Orthographic;
            if (ImGui::MenuItem("Orthographic", "5", orthographic)) {
                context.camera->projectionMode = orthographic ? Camera::Projection::Perspective
                                                              : Camera::Projection::Orthographic;
            }
        }

        ImGui::Separator();
        if (context.showUi) ImGui::MenuItem("Show panels", "Tab", context.showUi);
        if (context.turntable) ImGui::MenuItem("Turntable", "Space", context.turntable);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Display") && context.settings) {
        gfx::RenderSettings& settings = *context.settings;
        if (ImGui::BeginMenu("Shading")) {
            for (int i = 0; i < static_cast<int>(gfx::ShadingMode::Count); ++i) {
                const auto mode = static_cast<gfx::ShadingMode>(i);
                if (ImGui::MenuItem(gfx::shadingModeName(mode), nullptr, settings.shading == mode)) {
                    settings.shading = mode;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Wireframe", "W", &settings.wireframe);
        ImGui::MenuItem("Vertex normals", "N", &settings.showNormals);
        ImGui::MenuItem("Bounding boxes", "B", &settings.showBounds);
        ImGui::MenuItem("Grid", "G", &settings.showGrid);
        ImGui::MenuItem("Textures", "T", &settings.useTextures);
        ImGui::MenuItem("Flat shading", nullptr, &settings.flatShading);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        if (context.measureMode) ImGui::MenuItem("Measure", "M", context.measureMode);
        if (ImGui::MenuItem("Show all meshes", "Alt+H", false, context.renderer != nullptr)) {
            context.renderer->showAllMeshes();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Keyboard shortcuts", nullptr, &showShortcuts_);
        ImGui::MenuItem("About", nullptr, &showAbout_);
        ImGui::MenuItem("ImGui demo", nullptr, &showDemo_);
        ImGui::EndMenu();
    }

    // Right-aligned frame rate.
    const std::string fps = std::format("{:5.1f} fps", context.framesPerSecond);
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(fps.c_str()).x - 16.0f);
    ImGui::TextDisabled("%s", fps.c_str());

    ImGui::EndMainMenuBar();
}

void UiLayer::drawOutliner(UiContext& context) {
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 340), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Outliner")) {
        ImGui::End();
        return;
    }

    if (!context.scene || context.scene->empty()) {
        ImGui::TextDisabled("Nothing loaded.");
        ImGui::Spacing();
        ImGui::TextWrapped("Drop a file on the window, or use File > Open.");
        ImGui::End();
        return;
    }

    const scene::Scene& scene = *context.scene;

    // Recursive lambda over the node hierarchy.
    const auto drawNode = [&](auto&& self, int nodeIndex) -> void {
        const scene::Node& node = scene.nodes[static_cast<std::size_t>(nodeIndex)];

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
        if (node.children.empty() && node.meshes.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (nodeIndex == 0) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (context.selectedNode && *context.selectedNode == nodeIndex) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(nodeIndex);
        const bool open = ImGui::TreeNodeEx("##node", flags, "%s", node.name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            if (context.selectedNode) *context.selectedNode = nodeIndex;
        }

        if (open) {
            for (int meshIndex : node.meshes) {
                if (meshIndex < 0 || meshIndex >= static_cast<int>(scene.meshes.size())) continue;
                const scene::Mesh& mesh = scene.meshes[static_cast<std::size_t>(meshIndex)];

                ImGui::PushID(meshIndex + 100000);
                bool visible = context.renderer ? context.renderer->meshVisible(meshIndex) : true;
                if (ImGui::Checkbox("##visible", &visible) && context.renderer) {
                    context.renderer->setMeshVisible(meshIndex, visible);
                }
                ImGui::SameLine();

                const bool selected = context.selectedMesh && *context.selectedMesh == meshIndex;
                const std::string label =
                    std::format("{}  ({} tris)", mesh.name, humanCount(mesh.primitiveCount()));
                if (ImGui::Selectable(label.c_str(), selected)) {
                    if (context.selectedMesh) *context.selectedMesh = meshIndex;
                    if (context.selectedNode) *context.selectedNode = nodeIndex;
                    if (context.renderer) context.renderer->setSelectedMesh(meshIndex);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (context.frameSelection) context.frameSelection();
                }
                ImGui::PopID();
            }

            for (int child : node.children) self(self, child);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };

    if (!scene.nodes.empty()) drawNode(drawNode, 0);
    ImGui::End();
}

void UiLayer::drawInspector(UiContext& context) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkSize.x - 320, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    const int meshIndex = context.selectedMesh ? *context.selectedMesh : -1;
    if (!context.scene || meshIndex < 0 ||
        meshIndex >= static_cast<int>(context.scene->meshes.size())) {
        ImGui::TextDisabled("Select a mesh in the outliner,");
        ImGui::TextDisabled("or click one in the viewport.");
        ImGui::End();
        return;
    }

    const scene::Scene& scene = *context.scene;
    const scene::Mesh& mesh = scene.meshes[static_cast<std::size_t>(meshIndex)];

    ImGui::SeparatorText("Mesh");
    ImGui::Text("Name       %s", mesh.name.c_str());
    ImGui::Text("Vertices   %s", humanCount(mesh.vertices.size()).c_str());
    ImGui::Text("Primitives %s", humanCount(mesh.primitiveCount()).c_str());
    const char* topology = mesh.topology == scene::Topology::Triangles ? "triangles"
                           : mesh.topology == scene::Topology::Lines   ? "lines"
                                                                       : "points";
    ImGui::Text("Topology   %s", topology);
    ImGui::Text("Indexed    %s", mesh.indices.empty() ? "no" : "yes");

    if (mesh.bounds.valid()) {
        const vec3 size = mesh.bounds.size();
        const vec3 center = mesh.bounds.center();
        ImGui::SeparatorText("Bounds (object space)");
        ImGui::Text("Size    %.4g  %.4g  %.4g", size.x, size.y, size.z);
        ImGui::Text("Center  %.4g  %.4g  %.4g", center.x, center.y, center.z);
        ImGui::Text("Min     %.4g  %.4g  %.4g", mesh.bounds.min.x, mesh.bounds.min.y,
                    mesh.bounds.min.z);
        ImGui::Text("Max     %.4g  %.4g  %.4g", mesh.bounds.max.x, mesh.bounds.max.y,
                    mesh.bounds.max.z);
    }

    if (mesh.material >= 0 && mesh.material < static_cast<int>(scene.materials.size())) {
        const scene::Material& material = scene.materials[static_cast<std::size_t>(mesh.material)];
        ImGui::SeparatorText("Material");
        ImGui::Text("Name       %s", material.name.c_str());

        float baseColor[4] = {material.baseColor.r, material.baseColor.g, material.baseColor.b,
                              material.baseColor.a};
        ImGui::ColorEdit4("Base colour", baseColor,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoPicker);
        ImGui::Text("Metallic   %.3f", material.metallic);
        ImGui::Text("Roughness  %.3f", material.roughness);
        ImGui::Text("Emissive   %.3f %.3f %.3f", material.emissive.r, material.emissive.g,
                    material.emissive.b);
        const char* alphaMode = material.alphaMode == scene::AlphaMode::Opaque  ? "opaque"
                                : material.alphaMode == scene::AlphaMode::Mask ? "mask"
                                                                                : "blend";
        ImGui::Text("Alpha      %s", alphaMode);
        ImGui::Text("Two sided  %s", material.doubleSided ? "yes" : "no");

        ImGui::SeparatorText("Textures");
        const std::pair<const char*, int> slots[] = {
            {"Base colour", material.baseColorTexture},
            {"Metal/rough", material.metallicRoughnessTexture},
            {"Normal", material.normalTexture},
            {"Emissive", material.emissiveTexture},
            {"Occlusion", material.occlusionTexture},
        };
        for (const auto& [label, image] : slots) {
            if (image >= 0 && image < static_cast<int>(scene.images.size())) {
                const scene::Image& texture = scene.images[static_cast<std::size_t>(image)];
                ImGui::Text("%-12s %s (%dx%d)", label, texture.name.c_str(), texture.width,
                            texture.height);
            } else {
                ImGui::TextDisabled("%-12s -", label);
            }
        }
    }

    ImGui::End();
}

void UiLayer::drawRenderSettings(UiContext& context) {
    if (!context.settings) return;
    gfx::RenderSettings& settings = *context.settings;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkSize.x - 320, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310, 380), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Display")) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginCombo("Shading", gfx::shadingModeName(settings.shading))) {
        for (int i = 0; i < static_cast<int>(gfx::ShadingMode::Count); ++i) {
            const auto mode = static_cast<gfx::ShadingMode>(i);
            if (ImGui::Selectable(gfx::shadingModeName(mode), settings.shading == mode)) {
                settings.shading = mode;
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::CollapsingHeader("Overlays", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Wireframe", &settings.wireframe);
        ImGui::SameLine();
        ImGui::Checkbox("Bounds", &settings.showBounds);
        ImGui::Checkbox("Normals", &settings.showNormals);
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &settings.showGrid);
        if (settings.showNormals) {
            ImGui::SliderFloat("Length", &settings.normalLength, 0.001f, 0.2f, "%.3f");
        }
        ImGui::SliderFloat("Point size", &settings.pointSize, 1.0f, 16.0f, "%.1f");
        colorEdit("Wire colour", settings.wireColor);
    }

    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Textures", &settings.useTextures);
        ImGui::SameLine();
        ImGui::Checkbox("Vertex colours", &settings.useVertexColors);
        ImGui::Checkbox("Flat shading", &settings.flatShading);
        helpMarker("Derives a facet normal per pixel. Useful for checking CAD tessellation.");
        ImGui::Checkbox("Force two-sided", &settings.doubleSidedOverride);
        helpMarker("Disables backface culling, which reveals inverted normals.");
    }

    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Follow camera", &settings.lightFollowsCamera);
        ImGui::SliderAngle("Light yaw", &settings.lightYaw, -180.0f, 180.0f);
        ImGui::SliderAngle("Light pitch", &settings.lightPitch, -89.0f, 89.0f);
        ImGui::SliderFloat("Intensity", &settings.lightIntensity, 0.0f, 10.0f);
        ImGui::SliderFloat("Ambient", &settings.ambientIntensity, 0.0f, 2.0f);
        ImGui::SliderFloat("Exposure", &settings.exposure, 0.05f, 5.0f);
        ImGui::Checkbox("Tone map", &settings.tonemap);
    }

    if (ImGui::CollapsingHeader("Background")) {
        ImGui::Checkbox("Draw background", &settings.showBackground);
        colorEdit("Top", settings.backgroundTop);
        colorEdit("Bottom", settings.backgroundBottom);
        colorEdit("Grid", settings.gridColor);
        if (ImGui::Button("Studio")) {
            settings.backgroundTop = vec3(0.20f, 0.22f, 0.26f);
            settings.backgroundBottom = vec3(0.06f, 0.07f, 0.09f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Paper")) {
            settings.backgroundTop = vec3(0.95f, 0.95f, 0.96f);
            settings.backgroundBottom = vec3(0.78f, 0.79f, 0.82f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Black")) {
            settings.backgroundTop = vec3(0.0f);
            settings.backgroundBottom = vec3(0.0f);
        }
    }

    if (context.camera && ImGui::CollapsingHeader("Camera")) {
        Camera& camera = *context.camera;
        bool orthographic = camera.projectionMode == Camera::Projection::Orthographic;
        if (ImGui::Checkbox("Orthographic", &orthographic)) {
            camera.projectionMode =
                orthographic ? Camera::Projection::Orthographic : Camera::Projection::Perspective;
        }
        ImGui::SliderFloat("Field of view", &camera.fovDegrees, 10.0f, 120.0f, "%.0f deg");
        ImGui::Text("Distance  %.4g", camera.distance);
        ImGui::Text("Target    %.3g %.3g %.3g", camera.target.x, camera.target.y, camera.target.z);
        if (ImGui::Button("Frame all") && context.frameAll) context.frameAll();
    }

    ImGui::End();
}

void UiLayer::drawStats(UiContext& context) {
    ImGui::SetNextWindowPos(ImVec2(10, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 290), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Statistics")) {
        ImGui::End();
        return;
    }

    // Rotate the ring buffer so the plot reads left to right in time order.
    std::vector<float> ordered(frameTimes_.size());
    for (std::size_t i = 0; i < frameTimes_.size(); ++i) {
        ordered[i] = frameTimes_[(frameTimeCursor_ + i) % frameTimes_.size()];
    }
    const std::string overlay = std::format("{:.2f} ms", context.frameMilliseconds);
    ImGui::PlotLines("##frametime", ordered.data(), static_cast<int>(ordered.size()), 0,
                     overlay.c_str(), 0.0f, 33.3f, ImVec2(-1, 50));

    if (context.scene && !context.scene->empty()) {
        const scene::Stats& stats = context.scene->stats;
        ImGui::SeparatorText("Scene");
        ImGui::Text("Meshes      %s", humanCount(stats.meshCount).c_str());
        ImGui::Text("Vertices    %s", humanCount(stats.vertexCount).c_str());
        ImGui::Text("Triangles   %s", humanCount(stats.triangleCount).c_str());
        ImGui::Text("Materials   %s", humanCount(stats.materialCount).c_str());
        ImGui::Text("Textures    %s", humanCount(stats.imageCount).c_str());
        ImGui::Text("Nodes       %s", humanCount(stats.nodeCount).c_str());
        ImGui::Text("Load time   %.3f s", stats.loadSeconds);
        ImGui::Text("Importer    %s", context.scene->importerName.c_str());
    } else {
        ImGui::TextDisabled("No scene loaded.");
    }

    if (context.renderer) {
        const gfx::FrameStats& stats = context.renderer->stats();
        ImGui::SeparatorText("Frame");
        ImGui::Text("Draw calls  %d", stats.drawCalls);
        ImGui::Text("Triangles   %s", humanCount(stats.trianglesDrawn).c_str());
        ImGui::Text("GPU memory  %s", io::formatBytes(stats.gpuBytes).c_str());
        ImGui::Text("Upload      %.3f s", stats.gpuUploadSeconds);
    }

    ImGui::End();
}

void UiLayer::drawMeasurePanel(UiContext& context) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkSize.x * 0.5f - 160.0f, 40.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Measure", context.measureMode)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Click the model to drop points. Each consecutive pair is measured.");
    ImGui::Separator();

    if (!context.renderer) {
        ImGui::End();
        return;
    }

    const std::vector<vec3>& points = context.renderer->measurePoints();
    if (points.empty()) {
        ImGui::TextDisabled("No points yet.");
    }

    float total = 0.0f;
    for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
        const vec3 delta = points[i + 1] - points[i];
        const float length = glm::length(delta);
        total += length;
        ImGui::Text("%zu.  %.6g", i / 2 + 1, static_cast<double>(length));
        ImGui::SameLine();
        ImGui::TextDisabled("(dx %.4g  dy %.4g  dz %.4g)", delta.x, delta.y, delta.z);
    }
    if (points.size() % 2 == 1) {
        const vec3& pending = points.back();
        ImGui::TextDisabled("pending: %.4g %.4g %.4g", pending.x, pending.y, pending.z);
    }
    if (points.size() >= 2) {
        ImGui::Separator();
        ImGui::Text("Total  %.6g", static_cast<double>(total));
    }

    if (ImGui::Button("Clear")) context.renderer->measurePoints().clear();
    ImGui::SameLine();
    if (ImGui::Button("Undo") && !context.renderer->measurePoints().empty()) {
        context.renderer->measurePoints().pop_back();
    }

    ImGui::End();
}

void UiLayer::drawFileBrowser(UiContext& context) {
    ImGui::SetNextWindowSize(ImVec2(620, 440), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Open model", &showBrowser_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    std::error_code ec;
    ImGui::TextDisabled("%s", browserDirectory_.string().c_str());

    if (ImGui::Button("Up") && browserDirectory_.has_parent_path()) {
        browserDirectory_ = browserDirectory_.parent_path();
        browserSelection_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Home")) {
        if (const char* home = std::getenv("HOME")) browserDirectory_ = home;
    }
    ImGui::SameLine();
    static char pathBuffer[1024] = "";
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##path", "type a path and press Open", pathBuffer, sizeof(pathBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Open##typed") && pathBuffer[0] != '\0') {
        const std::filesystem::path typed(pathBuffer);
        if (std::filesystem::is_directory(typed, ec)) {
            browserDirectory_ = typed;
        } else if (context.openFile) {
            context.openFile(typed);
            showBrowser_ = false;
        }
    }

    ImGui::Separator();

    if (ImGui::BeginChild("##entries", ImVec2(0, -32), ImGuiChildFlags_Borders)) {
        std::vector<std::filesystem::directory_entry> directories;
        std::vector<std::filesystem::directory_entry> files;

        for (const auto& entry : std::filesystem::directory_iterator(
                 browserDirectory_, std::filesystem::directory_options::skip_permission_denied, ec)) {
            const std::string name = entry.path().filename().string();
            if (!name.empty() && name.front() == '.') continue;  // hide dotfiles
            if (entry.is_directory(ec)) {
                directories.push_back(entry);
            } else if (io::ImporterRegistry::instance().canLoad(entry.path())) {
                files.push_back(entry);
            }
        }

        const auto byName = [](const auto& a, const auto& b) {
            return io::toLower(a.path().filename().string()) <
                   io::toLower(b.path().filename().string());
        };
        std::sort(directories.begin(), directories.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        for (const auto& entry : directories) {
            const std::string label = "[dir]  " + entry.path().filename().string();
            if (ImGui::Selectable(label.c_str()) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                browserDirectory_ = entry.path();
                browserSelection_.clear();
            }
        }
        for (const auto& entry : files) {
            const std::string name = entry.path().filename().string();
            const bool selected = browserSelection_ == name;
            const std::uintmax_t size = entry.file_size(ec);
            const std::string label = std::format("{:<40} {}", name, io::formatBytes(size));
            if (ImGui::Selectable(label.c_str(), selected)) browserSelection_ = name;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (context.openFile) context.openFile(entry.path());
                showBrowser_ = false;
            }
        }

        if (directories.empty() && files.empty()) {
            ImGui::TextDisabled("No readable models in this folder.");
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Open", ImVec2(120, 0)) && !browserSelection_.empty()) {
        if (context.openFile) context.openFile(browserDirectory_ / browserSelection_);
        showBrowser_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) showBrowser_ = false;

    ImGui::End();
}

void UiLayer::drawExportDialog(UiContext& context) {
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Export", &showExport_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    const auto formats = io::ExporterRegistry::instance().supportedFormats();
    if (formats.empty()) {
        ImGui::TextDisabled("No exporters are available in this build.");
        ImGui::End();
        return;
    }

    exportFormatIndex_ = std::clamp(exportFormatIndex_, 0, static_cast<int>(formats.size()) - 1);
    const io::FormatInfo& selected = formats[static_cast<std::size_t>(exportFormatIndex_)];

    if (ImGui::BeginCombo("Format",
                          std::format(".{} - {}", selected.extension, selected.description).c_str())) {
        for (int i = 0; i < static_cast<int>(formats.size()); ++i) {
            const io::FormatInfo& format = formats[static_cast<std::size_t>(i)];
            const std::string label = std::format(".{} - {}", format.extension, format.description);
            if (ImGui::Selectable(label.c_str(), exportFormatIndex_ == i)) exportFormatIndex_ = i;
        }
        ImGui::EndCombo();
    }

    // Seed the output path from the loaded file the first time round.
    if (exportPath_.empty() && context.scene && !context.scene->sourcePath.empty()) {
        exportPath_ = std::filesystem::path(context.scene->sourcePath).replace_extension().string();
    }

    static char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "%s", exportPath_.c_str());
    if (ImGui::InputText("Path (no extension)", buffer, sizeof(buffer))) exportPath_ = buffer;

    ImGui::Checkbox("ASCII variant", &exportAscii_);
    helpMarker("Applies to formats that have both, such as STL, PLY and glTF/GLB.");

    const std::string fullPath = exportPath_.empty()
                                     ? std::string()
                                     : std::format("{}.{}", exportPath_, selected.extension);
    ImGui::TextDisabled("Writes: %s", fullPath.empty() ? "(set a path)" : fullPath.c_str());

    ImGui::Separator();
    ImGui::BeginDisabled(fullPath.empty() || !context.scene);
    if (ImGui::Button("Export", ImVec2(120, 0))) {
        if (context.exportFile) context.exportFile(fullPath, exportAscii_);
        showExport_ = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) showExport_ = false;

    ImGui::End();
}

void UiLayer::drawShortcuts() {
    ImGui::SetNextWindowSize(ImVec2(430, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Keyboard & mouse", &showShortcuts_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    const std::pair<const char*, const char*> rows[] = {
        {"Left drag / middle drag", "Orbit"},
        {"Shift + drag, right drag", "Pan"},
        {"Wheel", "Zoom"},
        {"Left click", "Select mesh (or drop a measure point)"},
        {"F / Shift+F", "Frame all / frame selection"},
        {"1 3 7 0", "Front / right / top / isometric"},
        {"Ctrl + 1 3 7", "Back / left / bottom"},
        {"5", "Toggle orthographic"},
        {"W N B G T", "Wireframe, normals, bounds, grid, textures"},
        {"C", "Cycle shading mode"},
        {"M", "Measure tool"},
        {"H / Alt+H", "Hide selected / show all"},
        {"Space", "Turntable"},
        {"Tab", "Hide the panels (fullscreen viewport)"},
        {"F11", "Fullscreen window"},
        {"F12", "Screenshot"},
        {"Ctrl+O / Ctrl+E", "Open / export"},
        {"Ctrl+R", "Reload the current file"},
        {"Esc", "Clear the selection"},
    };

    if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const auto& [keys, action] : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(keys);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", action);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void UiLayer::drawStatusBar(UiContext& context) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    ImGui::SetNextWindowBgAlpha(0.85f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar;

    if (ImGui::Begin("##status", nullptr, flags)) {
        ImGui::TextUnformatted(context.statusMessage.c_str());
        if (context.measureMode && *context.measureMode) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "  [measure]");
        }
    }
    ImGui::End();
}

}  // namespace tessera::ui

#else  // !TESSERA_WITH_UI

// Headless / no-UI build: the viewer still runs, driven entirely by the
// keyboard, so the application code needs no conditional compilation.
namespace tessera::ui {

bool UiLayer::initialize(GLFWwindow*, std::string&) { return true; }
void UiLayer::shutdown() {}
void UiLayer::beginFrame() {}
void UiLayer::draw(UiContext&) {}
void UiLayer::endFrame() {}
bool UiLayer::wantsMouse() const { return false; }
bool UiLayer::wantsKeyboard() const { return false; }
void UiLayer::openFileBrowser() {}
void UiLayer::openExportDialog() {}
void UiLayer::drawMenuBar(UiContext&) {}
void UiLayer::drawOutliner(UiContext&) {}
void UiLayer::drawInspector(UiContext&) {}
void UiLayer::drawRenderSettings(UiContext&) {}
void UiLayer::drawStats(UiContext&) {}
void UiLayer::drawMeasurePanel(UiContext&) {}
void UiLayer::drawFileBrowser(UiContext&) {}
void UiLayer::drawExportDialog(UiContext&) {}
void UiLayer::drawStatusBar(UiContext&) {}
void UiLayer::drawShortcuts() {}

}  // namespace tessera::ui

#endif  // TESSERA_WITH_UI
