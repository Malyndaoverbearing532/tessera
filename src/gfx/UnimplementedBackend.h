#pragma once

#include "gfx/RenderBackend.h"

#include <format>

namespace tessera::gfx {

/// Base for backends whose device detection works but whose renderer is not
/// written yet.
///
/// It implements the whole `IRenderBackend` contract with correct bookkeeping
/// and refuses only at `initialize()`, with a message naming exactly what is
/// missing. That keeps `--list-backends` truthful about which GPUs are present
/// without pretending the pixels can be drawn, and it means finishing a backend
/// is a matter of overriding the three drawing methods.
class UnimplementedBackend : public IRenderBackend {
public:
    UnimplementedBackend(BackendId id, std::string key, std::string displayName)
        : id_(id), key_(std::move(key)), displayName_(std::move(displayName)) {}

    [[nodiscard]] BackendInfo info() const override {
        BackendInfo result;
        result.id = id_;
        result.key = key_;
        result.displayName = displayName_;
        result.compiledIn = true;
        result.available = false;
        result.status = detectStatus();
        return result;
    }

    [[nodiscard]] WindowRequirements windowRequirements(int samples) const override {
        WindowRequirements requirements;
        requirements.needsOpenGLContext = false;  // these APIs own the surface themselves
        requirements.samples = samples;
        return requirements;
    }

    [[nodiscard]] bool initialize(GLFWwindow*, std::string& error) override {
        error = std::format("the {} backend has no renderer yet ({}). Use --backend opengl.",
                            displayName_, detectStatus());
        return false;
    }

    void shutdown() override {}

    void setScene(const scene::Scene* scene) override {
        scene_ = scene;
        meshVisible_.assign(scene ? scene->meshes.size() : 0, true);
        selectedMesh_ = -1;
        measurePoints_.clear();
    }

    void render(const Camera&, const RenderSettings&, int, int) override {}
    void present(GLFWwindow*) override {}

    [[nodiscard]] bool renderToImage(const Camera&, const RenderSettings&, int, int, int,
                                     std::vector<std::uint8_t>&, std::string& error) override {
        error = std::format("the {} backend cannot render yet", displayName_);
        return false;
    }

    [[nodiscard]] const FrameStats& stats() const override { return stats_; }

    void setMeshVisible(int meshIndex, bool visible) override {
        if (meshIndex >= 0 && meshIndex < static_cast<int>(meshVisible_.size())) {
            meshVisible_[static_cast<std::size_t>(meshIndex)] = visible;
        }
    }
    [[nodiscard]] bool meshVisible(int meshIndex) const override {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(meshVisible_.size())) return false;
        return meshVisible_[static_cast<std::size_t>(meshIndex)];
    }
    void showAllMeshes() override { meshVisible_.assign(meshVisible_.size(), true); }

    void setSelectedMesh(int meshIndex) override { selectedMesh_ = meshIndex; }
    [[nodiscard]] int selectedMesh() const override { return selectedMesh_; }

    std::vector<vec3>& measurePoints() override { return measurePoints_; }
    [[nodiscard]] const std::vector<vec3>& measurePoints() const override { return measurePoints_; }

protected:
    /// Probes the machine and returns a one-line summary: the device name when
    /// the hardware is present, or why it is not.
    [[nodiscard]] virtual std::string detectStatus() const = 0;

    const scene::Scene* scene_ = nullptr;
    std::vector<bool> meshVisible_;
    std::vector<vec3> measurePoints_;
    int selectedMesh_ = -1;
    FrameStats stats_;

private:
    BackendId id_;
    std::string key_;
    std::string displayName_;
};

}  // namespace tessera::gfx
