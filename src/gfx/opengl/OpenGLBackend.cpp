#include "gfx/opengl/OpenGLBackend.h"

#include "core/Log.h"
#include "gfx/opengl/Shaders.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>

namespace tessera::gfx {
namespace {

// Texture units, mirrored by the sampler uniforms in the mesh shader.
constexpr int kBaseColorUnit = 0;
constexpr int kMetallicRoughnessUnit = 1;
constexpr int kNormalUnit = 2;
constexpr int kEmissiveUnit = 3;
constexpr int kOcclusionUnit = 4;

/// The 12 edges of a unit cube spanning [0,1]^3, as line-list vertices.
constexpr float kUnitBoxEdges[] = {
    0, 0, 0,  1, 0, 0,   1, 0, 0,  1, 1, 0,   1, 1, 0,  0, 1, 0,   0, 1, 0,  0, 0, 0,
    0, 0, 1,  1, 0, 1,   1, 0, 1,  1, 1, 1,   1, 1, 1,  0, 1, 1,   0, 1, 1,  0, 0, 1,
    0, 0, 0,  0, 0, 1,   1, 0, 0,  1, 0, 1,   1, 1, 0,  1, 1, 1,   0, 1, 0,  0, 1, 1,
};

/// Three-point rig. Directions are returned in world space; when the rig
/// follows the camera the key light sits over the viewer's shoulder, which is
/// what keeps an arbitrary model readable the instant it loads.
std::array<vec3, 3> lightDirections(const Camera& camera, const RenderSettings& settings) {
    vec3 basisRight = vec3(1.0f, 0.0f, 0.0f);
    vec3 basisUp = vec3(0.0f, 1.0f, 0.0f);
    vec3 basisForward = vec3(0.0f, 0.0f, -1.0f);
    if (settings.lightFollowsCamera) {
        basisRight = camera.right();
        basisUp = camera.up();
        basisForward = camera.forward();
    }

    const auto direct = [&](float yaw, float pitch) {
        return glm::normalize(basisForward * std::cos(pitch) * std::cos(yaw) +
                              basisRight * std::cos(pitch) * std::sin(yaw) +
                              basisUp * -std::sin(pitch));
    };

    return {
        direct(settings.lightYaw, settings.lightPitch),          // key
        direct(-settings.lightYaw * 1.8f, -settings.lightPitch * 0.4f),  // fill
        direct(glm::pi<float>() + settings.lightYaw * 0.5f, -0.9f),      // rim
    };
}

}  // namespace

OpenGLBackend::~OpenGLBackend() { shutdown(); }

BackendInfo OpenGLBackend::info() const {
    BackendInfo result;
    result.id = BackendId::OpenGL;
    result.key = "opengl";
    result.displayName = "OpenGL 3.3 core";
    result.compiledIn = true;
    // Before initialisation there is no context to interrogate, so report the
    // backend as available and let initialize() produce the real diagnosis.
    result.available = true;
    result.status = initialized_ ? deviceName_ : "ready";
    return result;
}

WindowRequirements OpenGLBackend::windowRequirements(int samples) const {
    WindowRequirements requirements;
    requirements.needsOpenGLContext = true;
    requirements.contextMajor = 3;
    requirements.contextMinor = 3;
    requirements.coreProfile = true;
    // macOS only exposes core profiles through the forward-compatible flag.
    requirements.forwardCompatible = true;
    requirements.samples = samples;
    return requirements;
}

bool OpenGLBackend::initialize(GLFWwindow* window, std::string& error) {
    if (window != nullptr) {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
    }

    // glad is loaded here rather than in the application: whichever backend is
    // active owns its own API entry points.
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        error = "could not load OpenGL 3.3 function pointers";
        return false;
    }

    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!renderer || !version) {
        error = "no current OpenGL context";
        return false;
    }
    deviceName_ = std::format("{} | OpenGL {}", renderer, version);
    log::info("{}", deviceName_);

    const auto build = [&](Shader& shader, const char* vertex, const char* fragment,
                           const char* geometry, const char* name) {
        std::string localError;
        if (shader.build(vertex, fragment, geometry ? geometry : "", name, localError)) return true;
        error = localError;
        return false;
    };

    if (!build(meshShader_, shaders::kMeshVertex, shaders::kMeshFragment, nullptr, "mesh")) return false;
    if (!build(lineShader_, shaders::kLineVertex, shaders::kLineFragment, nullptr, "line")) return false;
    if (!build(normalsShader_, shaders::kNormalsVertex, shaders::kLineFragment,
               shaders::kNormalsGeometry, "normals")) {
        return false;
    }
    if (!build(gridShader_, shaders::kGridVertex, shaders::kGridFragment, nullptr, "grid")) return false;
    if (!build(backgroundShader_, shaders::kBackgroundVertex, shaders::kBackgroundFragment, nullptr,
               "background")) {
        return false;
    }

    whiteTexture_.createSolid(255, 255, 255, 255);
    flatNormalTexture_.createSolid(128, 128, 255, 255);
    blackTexture_.createSolid(0, 0, 0, 255);

    glGenVertexArrays(1, &emptyVao_);
    ensureLineGeometry();

    // Sampler bindings never change, so set them once.
    meshShader_.bind();
    meshShader_.set("uBaseColorMap", kBaseColorUnit);
    meshShader_.set("uMetallicRoughnessMap", kMetallicRoughnessUnit);
    meshShader_.set("uNormalMap", kNormalUnit);
    meshShader_.set("uEmissiveMap", kEmissiveUnit);
    meshShader_.set("uOcclusionMap", kOcclusionUnit);
    glUseProgram(0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    // Textures carry sRGB internal formats so sampling decodes them, lighting
    // runs linear, and the shaders encode once at the end. Leaving
    // GL_FRAMEBUFFER_SRGB off keeps that from being applied twice.
    glDisable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_MULTISAMPLE);

    TESSERA_GL_CHECK("OpenGLBackend::initialize");
    initialized_ = true;
    return true;
}

void OpenGLBackend::present(GLFWwindow* window) {
    if (window != nullptr) glfwSwapBuffers(window);
}

bool OpenGLBackend::renderToImage(const Camera& camera, const RenderSettings& settings, int width,
                                  int height, int samples, std::vector<std::uint8_t>& rgba,
                                  std::string& error) {
    Framebuffer target;
    if (!target.create(width, height, samples, error)) return false;

    target.bind();
    renderScene(camera, settings, width, height);
    target.resolve();

    if (!target.readPixels(rgba)) {
        error = "could not read the rendered image back";
        return false;
    }
    return true;
}

void OpenGLBackend::ensureLineGeometry() {
    if (boxVao_ != 0) return;
    glGenVertexArrays(1, &boxVao_);
    glBindVertexArray(boxVao_);
    glGenBuffers(1, &boxVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, boxVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kUnitBoxEdges), kUnitBoxEdges, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void OpenGLBackend::shutdown() {
    // Every GL object has to be released here, while the context is still
    // current. Anything left for a member destructor gets deleted after the
    // context is gone, which macOS quietly ignores and Mesa turns into a
    // segmentation fault.
    gpuMeshes_.clear();
    gpuTextures_.clear();
    whiteTexture_.destroy();
    flatNormalTexture_.destroy();
    blackTexture_.destroy();

    // Move-assigning an empty Shader deletes the program it was holding.
    meshShader_ = Shader{};
    lineShader_ = Shader{};
    normalsShader_ = Shader{};
    gridShader_ = Shader{};
    backgroundShader_ = Shader{};

    if (boxVbo_ != 0) glDeleteBuffers(1, &boxVbo_);
    if (boxVao_ != 0) glDeleteVertexArrays(1, &boxVao_);
    if (measureVbo_ != 0) glDeleteBuffers(1, &measureVbo_);
    if (measureVao_ != 0) glDeleteVertexArrays(1, &measureVao_);
    if (emptyVao_ != 0) glDeleteVertexArrays(1, &emptyVao_);
    boxVao_ = boxVbo_ = measureVao_ = measureVbo_ = emptyVao_ = 0;
    measureCapacity_ = 0;
    scene_ = nullptr;
    initialized_ = false;
}

void OpenGLBackend::setScene(const scene::Scene* scene) {
    const auto start = std::chrono::steady_clock::now();

    gpuMeshes_.clear();
    gpuTextures_.clear();
    meshVisible_.clear();
    drawList_.clear();
    selectedMesh_ = -1;
    measurePoints_.clear();
    scene_ = scene;
    stats_ = {};

    if (!scene) return;

    gpuMeshes_.resize(scene->meshes.size());
    meshVisible_.assign(scene->meshes.size(), true);
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < scene->meshes.size(); ++i) {
        gpuMeshes_[i].upload(scene->meshes[i]);
        bytes += gpuMeshes_[i].byteSize();
    }

    gpuTextures_.resize(scene->images.size());
    for (std::size_t i = 0; i < scene->images.size(); ++i) {
        gpuTextures_[i].upload(scene->images[i]);
        bytes += gpuTextures_[i].byteSize();
    }

    sceneBounds_ = scene->bounds();
    stats_.gpuBytes = bytes;
    stats_.gpuUploadSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    log::info("uploaded {} meshes and {} textures ({:.1f} MB) in {:.3f}s", gpuMeshes_.size(),
              gpuTextures_.size(), static_cast<double>(bytes) / (1024.0 * 1024.0),
              stats_.gpuUploadSeconds);
    TESSERA_GL_CHECK("OpenGLBackend::setScene");
}

void OpenGLBackend::setMeshVisible(int meshIndex, bool visible) {
    if (meshIndex >= 0 && meshIndex < static_cast<int>(meshVisible_.size())) {
        meshVisible_[static_cast<std::size_t>(meshIndex)] = visible;
    }
}

bool OpenGLBackend::meshVisible(int meshIndex) const {
    if (meshIndex < 0 || meshIndex >= static_cast<int>(meshVisible_.size())) return false;
    return meshVisible_[static_cast<std::size_t>(meshIndex)];
}

void OpenGLBackend::showAllMeshes() { meshVisible_.assign(meshVisible_.size(), true); }

void OpenGLBackend::buildDrawList(const Camera& camera) {
    drawList_.clear();
    if (!scene_) return;

    const vec3 eye = camera.position();
    scene_->forEachMeshInstance([&](int, int meshIndex, const mat4& world) {
        if (!meshVisible(meshIndex)) return;
        const scene::Mesh& mesh = scene_->meshes[static_cast<std::size_t>(meshIndex)];

        DrawItem item;
        item.meshIndex = meshIndex;
        item.world = world;
        item.viewDepth = glm::length(vec3(world * vec4(mesh.bounds.center(), 1.0f)) - eye);
        if (mesh.material >= 0 && mesh.material < static_cast<int>(scene_->materials.size())) {
            item.blended =
                scene_->materials[static_cast<std::size_t>(mesh.material)].alphaMode ==
                scene::AlphaMode::Blend;
        }
        drawList_.push_back(item);
    });

    // Back-to-front for the blended pass; the opaque pass keeps submission order.
    std::stable_sort(drawList_.begin(), drawList_.end(), [](const DrawItem& a, const DrawItem& b) {
        if (a.blended != b.blended) return !a.blended;
        return a.blended ? a.viewDepth > b.viewDepth : false;
    });
}

void OpenGLBackend::bindMaterial(const scene::Material& material, const RenderSettings& settings) {
    const auto bindSlot = [&](int imageIndex, int unit, const GpuTexture& fallback,
                              const char* flagName) {
        const bool available = settings.useTextures && imageIndex >= 0 &&
                               imageIndex < static_cast<int>(gpuTextures_.size()) &&
                               gpuTextures_[static_cast<std::size_t>(imageIndex)].valid();
        if (available) {
            gpuTextures_[static_cast<std::size_t>(imageIndex)].bind(unit);
        } else {
            fallback.bind(unit);
        }
        meshShader_.set(flagName, available ? 1 : 0);
    };

    bindSlot(material.baseColorTexture, kBaseColorUnit, whiteTexture_, "uHasBaseColorMap");
    bindSlot(material.metallicRoughnessTexture, kMetallicRoughnessUnit, whiteTexture_,
             "uHasMetallicRoughnessMap");
    bindSlot(material.normalTexture, kNormalUnit, flatNormalTexture_, "uHasNormalMap");
    bindSlot(material.emissiveTexture, kEmissiveUnit, blackTexture_, "uHasEmissiveMap");
    bindSlot(material.occlusionTexture, kOcclusionUnit, whiteTexture_, "uHasOcclusionMap");

    meshShader_.set("uBaseColor", material.baseColor);
    meshShader_.set("uEmissive", material.emissive);
    meshShader_.set("uMetallic", material.metallic);
    meshShader_.set("uRoughness", material.roughness);
    meshShader_.set("uNormalScale", material.normalScale);
    meshShader_.set("uOcclusionStrength", material.occlusionStrength);
    meshShader_.set("uAlphaCutoff", material.alphaCutoff);
    meshShader_.set("uAlphaMode", static_cast<int>(material.alphaMode));

    if (settings.doubleSidedOverride || material.doubleSided) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
}

void OpenGLBackend::drawBackground(const RenderSettings& settings) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    backgroundShader_.bind();
    backgroundShader_.set("uTop", settings.backgroundTop);
    backgroundShader_.set("uBottom", settings.backgroundBottom);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    ++stats_.drawCalls;
}

void OpenGLBackend::drawMeshes(const Camera& camera, const RenderSettings& settings,
                          const mat4& viewProjection, bool blendedPass) {
    if (!scene_) return;

    meshShader_.bind();
    meshShader_.set("uViewProjection", viewProjection);
    meshShader_.set("uCameraPosition", camera.position());
    meshShader_.set("uShadingMode", static_cast<int>(settings.shading));
    meshShader_.set("uUseVertexColors", settings.useVertexColors ? 1 : 0);
    meshShader_.set("uFlatShading", settings.flatShading ? 1 : 0);
    meshShader_.set("uExposure", settings.exposure);
    meshShader_.set("uTonemap", settings.tonemap ? 1 : 0);
    meshShader_.set("uPointSize", settings.pointSize);
    meshShader_.set("uAmbientTop", settings.backgroundTop);
    meshShader_.set("uAmbientBottom", settings.backgroundBottom);
    meshShader_.set("uAmbientIntensity", settings.ambientIntensity);

    const std::array<vec3, 3> lights = lightDirections(camera, settings);
    const std::array<vec3, 3> lightColors = {
        vec3(1.00f, 0.98f, 0.95f) * settings.lightIntensity,
        vec3(0.60f, 0.68f, 0.80f) * settings.lightIntensity * 0.35f,
        vec3(0.95f, 0.90f, 0.85f) * settings.lightIntensity * 0.25f,
    };
    for (int i = 0; i < 3; ++i) {
        meshShader_.set(std::format("uLightDirection[{}]", i), lights[static_cast<std::size_t>(i)]);
        meshShader_.set(std::format("uLightColor[{}]", i), lightColors[static_cast<std::size_t>(i)]);
    }

    if (blendedPass) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }

    for (const DrawItem& item : drawList_) {
        if (item.blended != blendedPass) continue;

        const scene::Mesh& mesh = scene_->meshes[static_cast<std::size_t>(item.meshIndex)];
        const GpuMesh& gpuMesh = gpuMeshes_[static_cast<std::size_t>(item.meshIndex)];
        if (!gpuMesh.valid()) continue;

        static const scene::Material kDefaultMaterial;
        const scene::Material& material =
            (mesh.material >= 0 && mesh.material < static_cast<int>(scene_->materials.size()))
                ? scene_->materials[static_cast<std::size_t>(mesh.material)]
                : kDefaultMaterial;
        bindMaterial(material, settings);

        meshShader_.set("uModel", item.world);
        meshShader_.set("uNormalMatrix", glm::transpose(glm::inverse(mat3(item.world))));

        gpuMesh.draw();
        ++stats_.drawCalls;
        stats_.trianglesDrawn += gpuMesh.primitiveCount();
    }

    if (blendedPass) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

void OpenGLBackend::drawGrid(const Camera& camera, const RenderSettings& settings,
                        const mat4& viewProjection) {
    // Snap the cell size to a power of ten near the scene scale so the grid
    // reads the same whether the model is millimetres or kilometres.
    const float radius = sceneBounds_.valid() ? std::max(sceneBounds_.radius(), 1e-3f) : 1.0f;
    const float exponent = std::floor(std::log10(radius));
    const float cellSize = std::pow(10.0f, exponent - 1.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    gridShader_.bind();
    gridShader_.set("uInverseViewProjection", glm::inverse(viewProjection));
    gridShader_.set("uViewProjection", viewProjection);
    gridShader_.set("uCameraPosition", camera.position());
    gridShader_.set("uGridColor", settings.gridColor);
    gridShader_.set("uCellSize", cellSize);
    gridShader_.set("uFadeDistance", camera.distance * 8.0f + radius * 10.0f);

    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++stats_.drawCalls;

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void OpenGLBackend::uploadMeasureGeometry() {
    if (measureVao_ == 0) {
        glGenVertexArrays(1, &measureVao_);
        glGenBuffers(1, &measureVbo_);
        glBindVertexArray(measureVao_);
        glBindBuffer(GL_ARRAY_BUFFER, measureVbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), nullptr);
        glBindVertexArray(0);
    }

    const std::size_t bytes = measurePoints_.size() * sizeof(vec3);
    glBindBuffer(GL_ARRAY_BUFFER, measureVbo_);
    if (bytes > measureCapacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), measurePoints_.data(),
                     GL_DYNAMIC_DRAW);
        measureCapacity_ = bytes;
    } else if (bytes > 0) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), measurePoints_.data());
    }
}

void OpenGLBackend::drawOverlays(const RenderSettings& settings, const mat4& viewProjection) {
    if (!scene_) return;

    lineShader_.bind();
    lineShader_.set("uViewProjection", viewProjection);

    // ---- wireframe --------------------------------------------------------
    if (settings.wireframe) {
        lineShader_.set("uColor", vec4(settings.wireColor, 1.0f));
        lineShader_.set("uDepthBias", 1e-4f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        for (const DrawItem& item : drawList_) {
            const GpuMesh& gpuMesh = gpuMeshes_[static_cast<std::size_t>(item.meshIndex)];
            if (!gpuMesh.valid()) continue;
            lineShader_.set("uModel", item.world);
            gpuMesh.draw();
            ++stats_.drawCalls;
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // ---- selection highlight ---------------------------------------------
    if (selectedMesh_ >= 0) {
        lineShader_.set("uColor", vec4(settings.selectionColor, 1.0f));
        lineShader_.set("uDepthBias", 3e-4f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        for (const DrawItem& item : drawList_) {
            if (item.meshIndex != selectedMesh_) continue;
            const GpuMesh& gpuMesh = gpuMeshes_[static_cast<std::size_t>(item.meshIndex)];
            if (!gpuMesh.valid()) continue;
            lineShader_.set("uModel", item.world);
            gpuMesh.draw();
            ++stats_.drawCalls;
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // ---- bounding boxes ---------------------------------------------------
    if (settings.showBounds) {
        lineShader_.set("uColor", vec4(0.35f, 0.75f, 0.45f, 1.0f));
        lineShader_.set("uDepthBias", 0.0f);
        glBindVertexArray(boxVao_);
        for (const DrawItem& item : drawList_) {
            const scene::Mesh& mesh = scene_->meshes[static_cast<std::size_t>(item.meshIndex)];
            if (!mesh.bounds.valid()) continue;
            // The shared unit cube is stretched onto each mesh's box.
            mat4 model = glm::translate(item.world, mesh.bounds.min);
            model = glm::scale(model, glm::max(mesh.bounds.size(), vec3(1e-6f)));
            lineShader_.set("uModel", model);
            glDrawArrays(GL_LINES, 0, 24);
            ++stats_.drawCalls;
        }
    }

    // ---- measurement ------------------------------------------------------
    if (!measurePoints_.empty()) {
        uploadMeasureGeometry();
        lineShader_.set("uModel", mat4(1.0f));
        lineShader_.set("uDepthBias", 0.0f);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(measureVao_);

        lineShader_.set("uColor", vec4(1.0f, 0.85f, 0.20f, 1.0f));
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(measurePoints_.size() / 2 * 2));

        lineShader_.set("uColor", vec4(1.0f, 0.35f, 0.20f, 1.0f));
        glPointSize(9.0f);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(measurePoints_.size()));

        glEnable(GL_DEPTH_TEST);
        stats_.drawCalls += 2;
    }

    // ---- vertex normals ---------------------------------------------------
    if (settings.showNormals) {
        const float radius = sceneBounds_.valid() ? std::max(sceneBounds_.radius(), 1e-4f) : 1.0f;
        normalsShader_.bind();
        normalsShader_.set("uViewProjection", viewProjection);
        normalsShader_.set("uLength", radius * settings.normalLength);
        normalsShader_.set("uShowTangents", 0);
        normalsShader_.set("uColor", vec4(0.30f, 0.65f, 1.0f, 1.0f));

        for (const DrawItem& item : drawList_) {
            const GpuMesh& gpuMesh = gpuMeshes_[static_cast<std::size_t>(item.meshIndex)];
            if (!gpuMesh.valid()) continue;
            normalsShader_.set("uModel", item.world);
            normalsShader_.set("uNormalMatrix", glm::transpose(glm::inverse(mat3(item.world))));
            gpuMesh.drawPoints();
            ++stats_.drawCalls;
        }
    }

    glBindVertexArray(0);
}

void OpenGLBackend::render(const Camera& camera, const RenderSettings& settings, int width, int height) {
    // Draw straight to the window; the UI layer composites on top afterwards.
    Framebuffer::bindDefault(width, height);
    renderScene(camera, settings, width, height);
}

void OpenGLBackend::renderScene(const Camera& camera, const RenderSettings& settings, int width,
                                int height) {
    stats_.drawCalls = 0;
    stats_.trianglesDrawn = 0;

    width = std::max(width, 1);
    height = std::max(height, 1);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const mat4 viewProjection = camera.viewProjection(aspect);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // Alpha 0 when the background is off, so headless renders come out with a
    // transparent surround ready for compositing.
    glClearColor(0.0f, 0.0f, 0.0f, settings.showBackground ? 1.0f : 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (settings.showBackground) drawBackground(settings);

    buildDrawList(camera);
    drawMeshes(camera, settings, viewProjection, /*blendedPass=*/false);
    if (settings.showGrid) drawGrid(camera, settings, viewProjection);
    drawMeshes(camera, settings, viewProjection, /*blendedPass=*/true);
    drawOverlays(settings, viewProjection);

    glDisable(GL_CULL_FACE);
    glBindVertexArray(0);
    glUseProgram(0);
    TESSERA_GL_CHECK("OpenGLBackend::render");
}

BackendPtr makeOpenGLBackend() { return std::make_unique<OpenGLBackend>(); }

}  // namespace tessera::gfx
