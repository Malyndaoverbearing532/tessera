#include "io/Exporter.h"

#include "core/Log.h"
#include "io/FileUtil.h"

#if defined(TESSERA_WITH_ASSIMP)
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <unordered_set>

namespace tessera::io {
namespace {

/// A world-space triangle soup, which is all the geometry-only formats need.
struct FlatMesh {
    std::vector<scene::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<int> materialPerTriangle;
};

FlatMesh flatten(const scene::Scene& source) {
    FlatMesh flat;
    source.forEachMeshInstance([&](int, int meshIndex, const mat4& world) {
        const scene::Mesh& mesh = source.meshes[static_cast<std::size_t>(meshIndex)];
        if (mesh.topology != scene::Topology::Triangles) return;

        const auto base = static_cast<std::uint32_t>(flat.vertices.size());
        const mat3 normalMatrix = glm::transpose(glm::inverse(mat3(world)));

        for (const scene::Vertex& vertex : mesh.vertices) {
            scene::Vertex transformed = vertex;
            transformed.position = vec3(world * vec4(vertex.position, 1.0f));
            transformed.normal = glm::normalize(normalMatrix * vertex.normal);
            flat.vertices.push_back(transformed);
        }

        if (mesh.indices.empty()) {
            for (std::uint32_t i = 0; i < mesh.vertices.size(); ++i) flat.indices.push_back(base + i);
        } else {
            for (std::uint32_t index : mesh.indices) flat.indices.push_back(base + index);
        }
        const std::size_t triangles = (mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size()) / 3;
        flat.materialPerTriangle.insert(flat.materialPerTriangle.end(), triangles, mesh.material);
    });
    return flat;
}

/// RAII wrapper so every early return closes the file.
class FileWriter {
public:
    explicit FileWriter(const std::filesystem::path& path)
        : file_(std::fopen(path.string().c_str(), "wb")) {}
    ~FileWriter() {
        if (file_) std::fclose(file_);
    }
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    [[nodiscard]] bool ok() const { return file_ != nullptr; }
    std::FILE* handle() const { return file_; }

    void write(std::string_view text) { std::fwrite(text.data(), 1, text.size(), file_); }
    void write(const void* data, std::size_t size) { std::fwrite(data, 1, size, file_); }

private:
    std::FILE* file_ = nullptr;
};

// ---------------------------------------------------------------------------
// OBJ
// ---------------------------------------------------------------------------
class ObjExporter final : public IExporter {
public:
    [[nodiscard]] std::string name() const override { return "obj"; }
    [[nodiscard]] std::vector<FormatInfo> formats() const override {
        return {{"obj", "Wavefront OBJ"}};
    }
    [[nodiscard]] int priority() const override { return 100; }

    [[nodiscard]] bool save(const scene::Scene& source, const std::filesystem::path& path,
                            const ExportOptions&, std::string& error) override {
        const FlatMesh flat = flatten(source);
        if (flat.indices.empty()) {
            error = "nothing to export (no triangle geometry)";
            return false;
        }

        FileWriter obj(path);
        if (!obj.ok()) {
            error = std::format("cannot write '{}'", path.string());
            return false;
        }

        const std::filesystem::path mtlPath = std::filesystem::path(path).replace_extension(".mtl");
        obj.write(std::format("# exported by tessera\nmtllib {}\n", mtlPath.filename().string()));

        std::string block;
        block.reserve(1 << 20);
        for (const scene::Vertex& v : flat.vertices) {
            block += std::format("v {:.6g} {:.6g} {:.6g}\n", v.position.x, v.position.y, v.position.z);
        }
        for (const scene::Vertex& v : flat.vertices) {
            block += std::format("vt {:.6g} {:.6g}\n", v.uv.x, v.uv.y);
        }
        for (const scene::Vertex& v : flat.vertices) {
            block += std::format("vn {:.6g} {:.6g} {:.6g}\n", v.normal.x, v.normal.y, v.normal.z);
        }
        obj.write(block);

        // Faces, grouped so consecutive triangles sharing a material emit one
        // usemtl line instead of one per face.
        block.clear();
        int activeMaterial = -2;
        for (std::size_t t = 0; t * 3 + 2 < flat.indices.size(); ++t) {
            const int material = t < flat.materialPerTriangle.size() ? flat.materialPerTriangle[t] : -1;
            if (material != activeMaterial) {
                activeMaterial = material;
                const std::string materialName =
                    (material >= 0 && material < static_cast<int>(source.materials.size()))
                        ? source.materials[static_cast<std::size_t>(material)].name
                        : "default";
                block += std::format("usemtl {}\n", materialName);
            }
            const std::uint32_t a = flat.indices[t * 3] + 1;
            const std::uint32_t b = flat.indices[t * 3 + 1] + 1;
            const std::uint32_t c = flat.indices[t * 3 + 2] + 1;
            block += std::format("f {0}/{0}/{0} {1}/{1}/{1} {2}/{2}/{2}\n", a, b, c);
            if (block.size() > (1 << 20)) {
                obj.write(block);
                block.clear();
            }
        }
        obj.write(block);

        FileWriter mtl(mtlPath);
        if (mtl.ok()) {
            mtl.write("# exported by tessera\n");
            const auto writeMaterial = [&](const scene::Material& material) {
                mtl.write(std::format(
                    "\nnewmtl {}\nKd {:.6g} {:.6g} {:.6g}\nKe {:.6g} {:.6g} {:.6g}\n"
                    "Pr {:.6g}\nPm {:.6g}\nd {:.6g}\n",
                    material.name, material.baseColor.r, material.baseColor.g, material.baseColor.b,
                    material.emissive.r, material.emissive.g, material.emissive.b,
                    material.roughness, material.metallic, material.baseColor.a));
                const auto writeMap = [&](const char* key, int image) {
                    if (image >= 0 && image < static_cast<int>(source.images.size())) {
                        mtl.write(std::format("{} {}\n", key,
                                              source.images[static_cast<std::size_t>(image)].name));
                    }
                };
                writeMap("map_Kd", material.baseColorTexture);
                writeMap("map_Bump", material.normalTexture);
                writeMap("map_Ke", material.emissiveTexture);
            };
            if (source.materials.empty()) {
                writeMaterial(scene::Material{.name = "default"});
            } else {
                for (const scene::Material& material : source.materials) writeMaterial(material);
            }
        } else {
            log::warn("could not write companion .mtl next to '{}'", path.string());
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// STL
// ---------------------------------------------------------------------------
class StlExporter final : public IExporter {
public:
    [[nodiscard]] std::string name() const override { return "stl"; }
    [[nodiscard]] std::vector<FormatInfo> formats() const override {
        return {{"stl", "Stereolithography"}};
    }
    [[nodiscard]] int priority() const override { return 100; }

    [[nodiscard]] bool save(const scene::Scene& source, const std::filesystem::path& path,
                            const ExportOptions& options, std::string& error) override {
        const FlatMesh flat = flatten(source);
        const std::size_t triangles = flat.indices.size() / 3;
        if (triangles == 0) {
            error = "nothing to export (no triangle geometry)";
            return false;
        }

        FileWriter file(path);
        if (!file.ok()) {
            error = std::format("cannot write '{}'", path.string());
            return false;
        }

        const auto faceNormal = [&](std::size_t t) {
            const vec3& a = flat.vertices[flat.indices[t * 3]].position;
            const vec3& b = flat.vertices[flat.indices[t * 3 + 1]].position;
            const vec3& c = flat.vertices[flat.indices[t * 3 + 2]].position;
            const vec3 n = glm::cross(b - a, c - a);
            return glm::length(n) > 1e-12f ? glm::normalize(n) : vec3(0.0f, 0.0f, 1.0f);
        };

        if (options.binary) {
            char header[80] = {};
            std::snprintf(header, sizeof(header), "exported by tessera");
            file.write(header, sizeof(header));

            const auto count = static_cast<std::uint32_t>(triangles);
            file.write(&count, sizeof(count));

            std::vector<char> record(50, 0);
            for (std::size_t t = 0; t < triangles; ++t) {
                const vec3 n = faceNormal(t);
                std::memcpy(record.data(), &n, 12);
                for (int k = 0; k < 3; ++k) {
                    const vec3& p = flat.vertices[flat.indices[t * 3 + static_cast<std::size_t>(k)]].position;
                    std::memcpy(record.data() + 12 + k * 12, &p, 12);
                }
                const std::uint16_t attribute = 0;
                std::memcpy(record.data() + 48, &attribute, 2);
                file.write(record.data(), record.size());
            }
        } else {
            std::string block = "solid tessera\n";
            for (std::size_t t = 0; t < triangles; ++t) {
                const vec3 n = faceNormal(t);
                block += std::format("facet normal {:.6g} {:.6g} {:.6g}\n  outer loop\n", n.x, n.y, n.z);
                for (int k = 0; k < 3; ++k) {
                    const vec3& p = flat.vertices[flat.indices[t * 3 + static_cast<std::size_t>(k)]].position;
                    block += std::format("    vertex {:.6g} {:.6g} {:.6g}\n", p.x, p.y, p.z);
                }
                block += "  endloop\nendfacet\n";
                if (block.size() > (1 << 20)) {
                    file.write(block);
                    block.clear();
                }
            }
            block += "endsolid tessera\n";
            file.write(block);
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// PLY
// ---------------------------------------------------------------------------
class PlyExporter final : public IExporter {
public:
    [[nodiscard]] std::string name() const override { return "ply"; }
    [[nodiscard]] std::vector<FormatInfo> formats() const override {
        return {{"ply", "Stanford Polygon"}};
    }
    [[nodiscard]] int priority() const override { return 100; }

    [[nodiscard]] bool save(const scene::Scene& source, const std::filesystem::path& path,
                            const ExportOptions& options, std::string& error) override {
        const FlatMesh flat = flatten(source);
        if (flat.vertices.empty()) {
            error = "nothing to export";
            return false;
        }

        FileWriter file(path);
        if (!file.ok()) {
            error = std::format("cannot write '{}'", path.string());
            return false;
        }

        const std::size_t triangles = flat.indices.size() / 3;
        file.write(std::format(
            "ply\nformat {} 1.0\ncomment exported by tessera\n"
            "element vertex {}\n"
            "property float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\n"
            "property float s\nproperty float t\n"
            "property uchar red\nproperty uchar green\nproperty uchar blue\n"
            "element face {}\nproperty list uchar uint vertex_indices\nend_header\n",
            options.binary ? "binary_little_endian" : "ascii", flat.vertices.size(), triangles));

        const auto toByte = [](float value) {
            return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };

        if (options.binary) {
            std::vector<char> record(8 * sizeof(float) + 3);
            for (const scene::Vertex& v : flat.vertices) {
                const float values[8] = {v.position.x, v.position.y, v.position.z, v.normal.x,
                                         v.normal.y,   v.normal.z,   v.uv.x,       v.uv.y};
                std::memcpy(record.data(), values, sizeof(values));
                record[sizeof(values)] = static_cast<char>(toByte(v.color.r));
                record[sizeof(values) + 1] = static_cast<char>(toByte(v.color.g));
                record[sizeof(values) + 2] = static_cast<char>(toByte(v.color.b));
                file.write(record.data(), record.size());
            }
            for (std::size_t t = 0; t < triangles; ++t) {
                const std::uint8_t count = 3;
                file.write(&count, 1);
                file.write(&flat.indices[t * 3], 3 * sizeof(std::uint32_t));
            }
        } else {
            std::string block;
            for (const scene::Vertex& v : flat.vertices) {
                block += std::format("{:.6g} {:.6g} {:.6g} {:.6g} {:.6g} {:.6g} {:.6g} {:.6g} {} {} {}\n",
                                     v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y,
                                     v.normal.z, v.uv.x, v.uv.y, toByte(v.color.r), toByte(v.color.g),
                                     toByte(v.color.b));
                if (block.size() > (1 << 20)) {
                    file.write(block);
                    block.clear();
                }
            }
            for (std::size_t t = 0; t < triangles; ++t) {
                block += std::format("3 {} {} {}\n", flat.indices[t * 3], flat.indices[t * 3 + 1],
                                     flat.indices[t * 3 + 2]);
                if (block.size() > (1 << 20)) {
                    file.write(block);
                    block.clear();
                }
            }
            file.write(block);
        }
        return true;
    }
};

#if defined(TESSERA_WITH_ASSIMP)
// ---------------------------------------------------------------------------
// Assimp - everything else (glTF/GLB, COLLADA, X3D, 3DS, FBX, ...)
// ---------------------------------------------------------------------------
class AssimpExporter final : public IExporter {
public:
    [[nodiscard]] std::string name() const override { return "assimp"; }

    [[nodiscard]] std::vector<FormatInfo> formats() const override {
        static const std::vector<FormatInfo> cached = [] {
            Assimp::Exporter exporter;
            std::vector<FormatInfo> result;
            for (std::size_t i = 0; i < exporter.GetExportFormatCount(); ++i) {
                const aiExportFormatDesc* description = exporter.GetExportFormatDescription(i);
                if (!description) continue;
                result.push_back({toLower(description->fileExtension), description->description});
            }
            return result;
        }();
        return cached;
    }

    [[nodiscard]] int priority() const override { return 0; }

    [[nodiscard]] bool save(const scene::Scene& source, const std::filesystem::path& path,
                            const ExportOptions& options, std::string& error) override;

private:
    /// Picks the Assimp format id whose extension matches `extension`.
    static std::string formatIdFor(const std::string& extension, bool preferBinary);
};

std::string AssimpExporter::formatIdFor(const std::string& extension, bool preferBinary) {
    Assimp::Exporter exporter;
    std::string fallback;
    for (std::size_t i = 0; i < exporter.GetExportFormatCount(); ++i) {
        const aiExportFormatDesc* description = exporter.GetExportFormatDescription(i);
        if (!description) continue;
        if (toLower(description->fileExtension) != extension) continue;

        const std::string id = description->id;
        // glTF ships as both "gltf2" (JSON) and "glb2" (binary); honour the flag.
        const bool isBinaryVariant = id.find("glb") != std::string::npos ||
                                     id.find("binary") != std::string::npos;
        if (isBinaryVariant == preferBinary) return id;
        if (fallback.empty()) fallback = id;
    }
    return fallback;
}

bool AssimpExporter::save(const scene::Scene& source, const std::filesystem::path& path,
                          const ExportOptions& options, std::string& error) {
    const std::string extension = extensionOf(path);
    const std::string formatId = formatIdFor(extension, options.binary);
    if (formatId.empty()) {
        error = std::format("assimp cannot write '.{}'", extension);
        return false;
    }

    // Build an aiScene that owns all of its arrays; ~aiScene frees them.
    auto out = std::make_unique<aiScene>();
    out->mRootNode = new aiNode();
    out->mRootNode->mName = aiString("root");

    const std::size_t materialCount = std::max<std::size_t>(1, source.materials.size());
    out->mNumMaterials = static_cast<unsigned int>(materialCount);
    out->mMaterials = new aiMaterial*[materialCount];
    for (std::size_t i = 0; i < materialCount; ++i) {
        auto* material = new aiMaterial();
        if (i < source.materials.size()) {
            const scene::Material& src = source.materials[i];
            aiString name(src.name);
            material->AddProperty(&name, AI_MATKEY_NAME);
            const aiColor4D diffuse(src.baseColor.r, src.baseColor.g, src.baseColor.b, src.baseColor.a);
            material->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
            const aiColor4D emissive(src.emissive.r, src.emissive.g, src.emissive.b, 1.0f);
            material->AddProperty(&emissive, 1, AI_MATKEY_COLOR_EMISSIVE);
            material->AddProperty(&src.metallic, 1, AI_MATKEY_METALLIC_FACTOR);
            material->AddProperty(&src.roughness, 1, AI_MATKEY_ROUGHNESS_FACTOR);
            const float opacity = src.baseColor.a;
            material->AddProperty(&opacity, 1, AI_MATKEY_OPACITY);
            if (src.baseColorTexture >= 0 &&
                src.baseColorTexture < static_cast<int>(source.images.size())) {
                aiString texture(source.images[static_cast<std::size_t>(src.baseColorTexture)].name);
                material->AddProperty(&texture, AI_MATKEY_TEXTURE_DIFFUSE(0));
            }
        } else {
            aiString name("default");
            material->AddProperty(&name, AI_MATKEY_NAME);
        }
        out->mMaterials[i] = material;
    }

    // One aiMesh per source mesh, with world transforms baked in. Exporting a
    // flat list keeps the writer paths simple and lossless for geometry.
    struct Instance {
        int mesh = -1;
        mat4 world{1.0f};
    };
    std::vector<Instance> instances;
    source.forEachMeshInstance([&](int, int meshIndex, const mat4& world) {
        if (source.meshes[static_cast<std::size_t>(meshIndex)].topology == scene::Topology::Triangles) {
            instances.push_back({meshIndex, world});
        }
    });

    if (instances.empty()) {
        error = "nothing to export (no triangle geometry)";
        return false;
    }

    out->mNumMeshes = static_cast<unsigned int>(instances.size());
    out->mMeshes = new aiMesh*[instances.size()];
    out->mRootNode->mNumMeshes = out->mNumMeshes;
    out->mRootNode->mMeshes = new unsigned int[instances.size()];

    for (std::size_t i = 0; i < instances.size(); ++i) {
        const scene::Mesh& src = source.meshes[static_cast<std::size_t>(instances[i].mesh)];
        const mat4& world = instances[i].world;
        const mat3 normalMatrix = glm::transpose(glm::inverse(mat3(world)));

        auto* mesh = new aiMesh();
        mesh->mName = aiString(src.name);
        mesh->mMaterialIndex = static_cast<unsigned int>(
            std::clamp<int>(src.material, 0, static_cast<int>(materialCount) - 1));
        mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

        mesh->mNumVertices = static_cast<unsigned int>(src.vertices.size());
        mesh->mVertices = new aiVector3D[src.vertices.size()];
        mesh->mNormals = new aiVector3D[src.vertices.size()];
        mesh->mTextureCoords[0] = new aiVector3D[src.vertices.size()];
        mesh->mNumUVComponents[0] = 2;
        for (std::size_t v = 0; v < src.vertices.size(); ++v) {
            const vec3 position = vec3(world * vec4(src.vertices[v].position, 1.0f));
            const vec3 normal = glm::normalize(normalMatrix * src.vertices[v].normal);
            mesh->mVertices[v] = aiVector3D(position.x, position.y, position.z);
            mesh->mNormals[v] = aiVector3D(normal.x, normal.y, normal.z);
            mesh->mTextureCoords[0][v] = aiVector3D(src.vertices[v].uv.x, src.vertices[v].uv.y, 0.0f);
        }

        const std::size_t indexCount = src.indices.empty() ? src.vertices.size() : src.indices.size();
        const std::size_t faceCount = indexCount / 3;
        mesh->mNumFaces = static_cast<unsigned int>(faceCount);
        mesh->mFaces = new aiFace[faceCount];
        for (std::size_t f = 0; f < faceCount; ++f) {
            aiFace& face = mesh->mFaces[f];
            face.mNumIndices = 3;
            face.mIndices = new unsigned int[3];
            for (std::size_t k = 0; k < 3; ++k) {
                face.mIndices[k] = src.indices.empty()
                                       ? static_cast<unsigned int>(f * 3 + k)
                                       : src.indices[f * 3 + k];
            }
        }

        out->mMeshes[i] = mesh;
        out->mRootNode->mMeshes[i] = static_cast<unsigned int>(i);
    }

    Assimp::Exporter exporter;
    if (exporter.Export(out.get(), formatId, path.string()) != AI_SUCCESS) {
        error = exporter.GetErrorString();
        if (error.empty()) error = std::format("assimp failed to write '{}'", path.string());
        return false;
    }
    return true;
}
#endif  // TESSERA_WITH_ASSIMP

}  // namespace

ExporterRegistry& ExporterRegistry::instance() {
    static ExporterRegistry registry;
    return registry;
}

void ExporterRegistry::add(ExporterPtr exporter) {
    if (!exporter) return;
    exporters_.push_back(std::move(exporter));
    std::stable_sort(exporters_.begin(), exporters_.end(),
                     [](const ExporterPtr& a, const ExporterPtr& b) {
                         return a->priority() > b->priority();
                     });
}

std::vector<FormatInfo> ExporterRegistry::supportedFormats() const {
    std::vector<FormatInfo> result;
    std::unordered_set<std::string> seen;
    for (const ExporterPtr& exporter : exporters_) {
        for (const FormatInfo& format : exporter->formats()) {
            if (seen.insert(format.extension).second) result.push_back(format);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const FormatInfo& a, const FormatInfo& b) { return a.extension < b.extension; });
    return result;
}

bool ExporterRegistry::save(const scene::Scene& source, const std::filesystem::path& path,
                            const ExportOptions& options, std::string& error) const {
    const std::string extension = extensionOf(path);
    if (extension.empty()) {
        error = "output path has no extension, so the format is ambiguous";
        return false;
    }

    std::string attempts;
    for (const ExporterPtr& exporter : exporters_) {
        const auto& supported = exporter->formats();
        const bool claims = std::any_of(supported.begin(), supported.end(),
                                        [&](const FormatInfo& f) { return f.extension == extension; });
        if (!claims) continue;

        std::string localError;
        if (exporter->save(source, path, options, localError)) {
            log::info("wrote '{}' via {}", path.string(), exporter->name());
            return true;
        }
        attempts += std::format("\n  {}: {}", exporter->name(), localError);
    }

    error = attempts.empty()
                ? std::format("no exporter for '.{}' (run with --export-formats to list)", extension)
                : std::format("failed to write '{}'{}", path.string(), attempts);
    return false;
}

void registerBuiltinExporters() {
    static bool done = false;
    if (done) return;
    done = true;

    ExporterRegistry& registry = ExporterRegistry::instance();
    registry.add(std::make_unique<ObjExporter>());
    registry.add(std::make_unique<StlExporter>());
    registry.add(std::make_unique<PlyExporter>());
#if defined(TESSERA_WITH_ASSIMP)
    registry.add(std::make_unique<AssimpExporter>());
#endif
}

}  // namespace tessera::io
