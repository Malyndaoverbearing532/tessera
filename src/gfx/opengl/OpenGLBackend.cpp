#include "gfx/opengl/OpenGLBackend.h"

#include "core/Log.h"
#include "gfx/opengl/Shaders.h"

#include <glm/gtc/type_ptr.hpp>

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
    batches_.clear();
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
    batches_.clear();
    drawListDirty_ = true;
    selectedMesh_ = -1;
    measurePoints_.clear();
    scene_ = scene;
    stats_ = {};

    if (!scene) return;

    const std::size_t meshCount = scene->meshes.size();
    gpuMeshes_.resize(meshCount);
    meshVisible_.assign(meshCount, true);
    transformBaked_.assign(meshCount, false);

    std::size_t bytes = 0;
    gpuTextures_.resize(scene->images.size());
    for (std::size_t i = 0; i < scene->images.size(); ++i) {
        gpuTextures_[i].upload(scene->images[i]);
        bytes += gpuTextures_[i].byteSize();
    }

    sceneBounds_ = scene->bounds();
    stats_.gpuBytes = bytes;

    // Geometry is uploaded by buildBatches() on the first frame instead of
    // here, because whether a mesh gets its own buffer or is merged into a
    // shared one depends on the draw list, which does not exist yet.
    drawListDirty_ = true;
    stats_.gpuUploadSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
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

void OpenGLBackend::showAllMeshes() {
    meshVisible_.assign(meshVisible_.size(), true);
}

void OpenGLBackend::rebuildDrawList() {
    drawList_.clear();
    drawListDirty_ = false;
    if (!scene_) return;

    // Every instance is listed, including hidden ones: batches are built from
    // this list and never rebuilt, so visibility is applied when drawing.
    scene_->forEachMeshInstance([&](int, int meshIndex, const mat4& world) {
        const scene::Mesh& mesh = scene_->meshes[static_cast<std::size_t>(meshIndex)];

        DrawItem item;
        item.meshIndex = meshIndex;
        item.material = mesh.material;
        // The true transform is kept here; buildBatches() decides whether it
        // gets folded into a buffer and replaced with the identity.
        item.world = world;
        item.normalMatrix = glm::transpose(glm::inverse(mat3(world)));
        item.worldBounds = mesh.bounds.transformed(world);
        if (mesh.material >= 0 && mesh.material < static_cast<int>(scene_->materials.size())) {
            item.blended =
                scene_->materials[static_cast<std::size_t>(mesh.material)].alphaMode ==
                scene::AlphaMode::Blend;
        }
        drawList_.push_back(item);
    });

    // Opaque geometry is grouped by material so the draw loop can skip
    // rebinding one it is already using: on a scene of thousands of meshes
    // sharing a handful of materials, that turns thousands of binds into a
    // handful. Blended geometry still has to go back to front for correctness,
    // so it cannot be grouped.
    std::stable_sort(drawList_.begin(), drawList_.end(), [](const DrawItem& a, const DrawItem& b) {
        if (a.blended != b.blended) return !a.blended;
        return a.material < b.material;
    });

    buildBatches();
}

void OpenGLBackend::buildBatches() {
    batches_.clear();
    if (!scene_) return;

    const auto start = std::chrono::steady_clock::now();
    const std::size_t meshCount = scene_->meshes.size();

    // How many places each mesh is drawn from. A mesh used once can have its
    // transform folded in; one used repeatedly cannot, because a single buffer
    // cannot hold two different world positions for the same vertices.
    std::vector<int> instances(meshCount, 0);
    for (const DrawItem& item : drawList_) {
        ++instances[static_cast<std::size_t>(item.meshIndex)];
    }

    const auto batchable = [&](const DrawItem& item) {
        const scene::Mesh& mesh = scene_->meshes[static_cast<std::size_t>(item.meshIndex)];
        return !item.blended && mesh.topology == scene::Topology::Triangles &&
               !mesh.vertices.empty() && instances[static_cast<std::size_t>(item.meshIndex)] == 1;
    };

    // drawList_ is already grouped by material, so equal materials are adjacent
    // and one pass produces one batch per material.
    std::vector<scene::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    int currentMaterial = -2;

    const auto flush = [&]() {
        if (indices.empty()) return;
        batches_.back().mesh.uploadMerged(vertices, indices);
        vertices.clear();
        indices.clear();
    };

    for (std::size_t i = 0; i < drawList_.size(); ++i) {
        DrawItem& item = drawList_[i];
        if (!batchable(item)) continue;

        if (item.material != currentMaterial) {
            flush();
            currentMaterial = item.material;
            batches_.push_back(Batch{});
            batches_.back().material = item.material;
        }

        const scene::Mesh& mesh = scene_->meshes[static_cast<std::size_t>(item.meshIndex)];
        const mat3 normalMatrix = glm::transpose(glm::inverse(mat3(item.world)));
        const auto baseVertex = static_cast<std::uint32_t>(vertices.size());

        // Vertices go in already transformed, so the whole batch shares one
        // identity model matrix and needs no per-mesh uniforms at all.
        vertices.reserve(vertices.size() + mesh.vertices.size());
        for (const scene::Vertex& source : mesh.vertices) {
            scene::Vertex vertex = source;
            vertex.position = vec3(item.world * vec4(source.position, 1.0f));
            vertex.normal = glm::normalize(normalMatrix * source.normal);
            vertex.tangent = vec4(glm::normalize(normalMatrix * vec3(source.tangent)),
                                  source.tangent.w);
            vertices.push_back(vertex);
        }

        item.batch = static_cast<int>(batches_.size()) - 1;
        item.vertexOffset = static_cast<GLsizei>(baseVertex);
        item.vertexCount = static_cast<GLsizei>(mesh.vertices.size());
        item.indexOffset = static_cast<GLsizei>(indices.size());

        if (mesh.indices.empty()) {
            for (std::uint32_t v = 0; v < mesh.vertices.size(); ++v) {
                indices.push_back(baseVertex + v);
            }
        } else {
            for (std::uint32_t index : mesh.indices) indices.push_back(baseVertex + index);
        }
        item.indexCount = static_cast<GLsizei>(indices.size()) - item.indexOffset;

        item.baked = true;
        item.world = mat4(1.0f);
        item.normalMatrix = mat3(1.0f);
        batches_.back().items.push_back(static_cast<int>(i));
    }
    flush();

    // Anything not merged still needs a buffer of its own. Single-instance
    // meshes can at least have their transform folded in.
    std::vector<bool> uploaded(meshCount, false);
    for (DrawItem& item : drawList_) {
        const auto meshIndex = static_cast<std::size_t>(item.meshIndex);
        if (item.batch >= 0) continue;

        const bool bake = instances[meshIndex] == 1;
        if (!uploaded[meshIndex]) {
            gpuMeshes_[meshIndex].upload(scene_->meshes[meshIndex],
                                         bake ? item.world : mat4(1.0f));
            uploaded[meshIndex] = true;
        }
        transformBaked_[meshIndex] = bake;
        if (bake) {
            item.baked = true;
            item.world = mat4(1.0f);
            item.normalMatrix = mat3(1.0f);
        }
    }

    std::size_t geometryBytes = 0;
    std::size_t batchedMeshes = 0;
    for (const Batch& batch : batches_) {
        geometryBytes += batch.mesh.byteSize();
        batchedMeshes += batch.items.size();
    }
    for (const GpuMesh& mesh : gpuMeshes_) geometryBytes += mesh.byteSize();

    std::size_t textureBytes = 0;
    for (const GpuTexture& texture : gpuTextures_) textureBytes += texture.byteSize();

    stats_.gpuBytes = geometryBytes + textureBytes;
    stats_.gpuUploadSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    log::info("uploaded {} meshes ({} merged into {} batches) and {} textures, {:.1f} MB in {:.3f}s",
              drawList_.size(), batchedMeshes, batches_.size(), gpuTextures_.size(),
              static_cast<double>(stats_.gpuBytes) / (1024.0 * 1024.0), stats_.gpuUploadSeconds);
    TESSERA_GL_CHECK("OpenGLBackend::buildBatches");
}

void OpenGLBackend::drawItemGeometry(const DrawItem& item) {
    if (item.batch >= 0) {
        const GpuMesh& merged = batches_[static_cast<std::size_t>(item.batch)].mesh;
        if (!merged.valid()) return;
        merged.bind();
        merged.drawRange(item.indexOffset, item.indexCount);
    } else {
        const GpuMesh& mesh = gpuMeshes_[static_cast<std::size_t>(item.meshIndex)];
        if (!mesh.valid()) return;
        mesh.draw();
    }
    ++stats_.drawCalls;
}

void OpenGLBackend::drawItemPoints(const DrawItem& item) {
    if (item.batch >= 0) {
        const GpuMesh& merged = batches_[static_cast<std::size_t>(item.batch)].mesh;
        if (!merged.valid()) return;
        merged.bind();
        merged.drawPointsRange(item.vertexOffset, item.vertexCount);
    } else {
        const GpuMesh& mesh = gpuMeshes_[static_cast<std::size_t>(item.meshIndex)];
        if (!mesh.valid()) return;
        mesh.drawPoints();
    }
    ++stats_.drawCalls;
}

void OpenGLBackend::drawBatch(const Batch& batch) {
    batch.mesh.bind();

    // Consecutive visible meshes share one draw call. A fully visible batch is
    // therefore a single call no matter how many meshes went into it, and the
    // worst case (alternating visibility) degrades gracefully to one per mesh.
    GLsizei runOffset = 0;
    GLsizei runCount = 0;

    const auto submit = [&]() {
        if (runCount == 0) return;
        batch.mesh.drawRange(runOffset, runCount);
        ++stats_.drawCalls;
        stats_.trianglesDrawn += static_cast<std::size_t>(runCount) / 3;
        runCount = 0;
    };

    for (int itemIndex : batch.items) {
        const DrawItem& item = drawList_[static_cast<std::size_t>(itemIndex)];
        const bool visible = !item.culled && meshVisible(item.meshIndex);
        if (!visible) {
            submit();
            continue;
        }
        if (runCount == 0) runOffset = item.indexOffset;
        runCount += item.indexCount;
    }
    submit();
}

void OpenGLBackend::updateDrawList(const Camera& camera, int width, int height) {
    if (drawListDirty_) rebuildDrawList();
    if (drawList_.empty()) return;

    const float aspect = static_cast<float>(std::max(width, 1)) / static_cast<float>(std::max(height, 1));
    Frustum frustum;
    frustum.extract(camera.viewProjection(aspect));

    const vec3 eye = camera.position();
    const auto firstBlended =
        std::find_if(drawList_.begin(), drawList_.end(), [](const DrawItem& i) { return i.blended; });

    for (auto it = drawList_.begin(); it != drawList_.end(); ++it) {
        it->culled = !frustum.intersects(it->worldBounds);
        // Only the blended half needs a distance, and only for sorting.
        if (it >= firstBlended && !it->culled) {
            it->viewDepth = glm::length(it->worldBounds.center() - eye);
        }
    }

    // Transparency has to be drawn back to front, so that subrange is re-sorted
    // whenever the camera moves. The opaque half stays grouped by material.
    std::sort(firstBlended, drawList_.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.viewDepth > b.viewDepth;
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

    const int wantCulling = (settings.doubleSidedOverride || material.doubleSided) ? 0 : 1;
    if (wantCulling != cullFaceEnabled_) {
        if (wantCulling == 1) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }
        cullFaceEnabled_ = wantCulling;
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

    // Another pass may have left different bindings in place, so trust nothing
    // carried over from last time.
    boundMaterial_ = -2;
    cullFaceEnabled_ = -1;

    // Resolved once per pass instead of hashed once per draw.
    const GLint modelLocation = meshShader_.locationOf("uModel");
    const GLint normalMatrixLocation = meshShader_.locationOf("uNormalMatrix");
    bool identityBound = false;

    static const scene::Material kDefaultMaterial;
    const auto materialAt = [&](int index) -> const scene::Material& {
        return (index >= 0 && index < static_cast<int>(scene_->materials.size()))
                   ? scene_->materials[static_cast<std::size_t>(index)]
                   : kDefaultMaterial;
    };

    // Merged batches carry only opaque geometry, so they belong to that pass.
    // Each costs one material bind and, when nothing in it is hidden or culled,
    // a single draw call regardless of how many meshes went in.
    if (!blendedPass) {
        for (const Batch& batch : batches_) {
            if (batch.material != boundMaterial_) {
                bindMaterial(materialAt(batch.material), settings);
                boundMaterial_ = batch.material;
            }
            if (!identityBound) {
                const mat4 identityModel(1.0f);
                const mat3 identityNormal(1.0f);
                glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(identityModel));
                glUniformMatrix3fv(normalMatrixLocation, 1, GL_FALSE, glm::value_ptr(identityNormal));
                identityBound = true;
            }
            drawBatch(batch);
        }
    }

    for (const DrawItem& item : drawList_) {
        if (item.blended != blendedPass || item.culled || item.batch >= 0) continue;
        if (!meshVisible(item.meshIndex)) continue;

        const GpuMesh& gpuMesh = gpuMeshes_[static_cast<std::size_t>(item.meshIndex)];
        if (!gpuMesh.valid()) continue;

        // Sorting put equal materials next to each other, so this skips almost
        // every material bind on a typical scene.
        if (item.material != boundMaterial_) {
            bindMaterial(materialAt(item.material), settings);
            boundMaterial_ = item.material;
        }

        // Consecutive baked meshes all want the identity, so upload it once.
        if (!item.baked || !identityBound) {
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(item.world));
            glUniformMatrix3fv(normalMatrixLocation, 1, GL_FALSE, glm::value_ptr(item.normalMatrix));
            identityBound = item.baked;
        }

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
            if (item.culled || !meshVisible(item.meshIndex)) continue;
            lineShader_.set("uModel", item.world);
            drawItemGeometry(item);
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
            lineShader_.set("uModel", item.world);
            drawItemGeometry(item);
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // ---- bounding boxes ---------------------------------------------------
    if (settings.showBounds) {
        lineShader_.set("uColor", vec4(0.35f, 0.75f, 0.45f, 1.0f));
        lineShader_.set("uDepthBias", 0.0f);
        glBindVertexArray(boxVao_);
        for (const DrawItem& item : drawList_) {
            if (item.culled || !item.worldBounds.valid()) continue;
            if (!meshVisible(item.meshIndex)) continue;
            // Uses the precomputed world-space box, not the object-space one
            // combined with item.world: a mesh whose transform was baked into
            // its vertex buffer carries an identity matrix here, and pairing
            // that with object-space bounds would draw the box in the wrong
            // place entirely.
            mat4 model = glm::translate(mat4(1.0f), item.worldBounds.min);
            model = glm::scale(model, glm::max(item.worldBounds.size(), vec3(1e-6f)));
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
            if (item.culled || !meshVisible(item.meshIndex)) continue;
            normalsShader_.set("uModel", item.world);
            normalsShader_.set("uNormalMatrix", item.normalMatrix);
            drawItemPoints(item);
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

    updateDrawList(camera, width, height);
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
