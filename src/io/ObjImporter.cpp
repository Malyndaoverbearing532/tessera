// Wavefront OBJ + MTL reader.
//
// This is one of the native fast paths: it reads the whole file once, parses
// numbers in place, and splits geometry into one mesh per material. It is a
// good deal quicker than the general-purpose backend on large OBJ dumps, which
// is the format people most often throw multi-million-triangle files at.

#include "core/Log.h"
#include "io/FileUtil.h"
#include "io/ImageLoader.h"
#include "io/Importer.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>

namespace tessera::io {
namespace {

/// A face corner references three independent index streams; the combination
/// is what becomes a unique GPU vertex.
struct Corner {
    int position = -1;
    int uv = -1;
    int normal = -1;

    bool operator==(const Corner& other) const {
        return position == other.position && uv == other.uv && normal == other.normal;
    }
};

struct CornerHash {
    std::size_t operator()(const Corner& c) const noexcept {
        // FNV-1a over the three indices.
        std::size_t h = 1469598103934665603ULL;
        for (int value : {c.position, c.uv, c.normal}) {
            h ^= static_cast<std::size_t>(static_cast<unsigned>(value));
            h *= 1099511628211ULL;
        }
        return h;
    }
};

/// Resolves OBJ's 1-based / negative index convention against a stream size.
int resolveIndex(long raw, std::size_t count) {
    if (raw > 0) return static_cast<int>(raw - 1);
    if (raw < 0) return static_cast<int>(static_cast<long>(count) + raw);
    return -1;
}

/// Ns (specular exponent) -> roughness, the usual Blinn-Phong approximation.
float shininessToRoughness(float shininess) {
    shininess = std::max(shininess, 0.0f);
    return std::clamp(std::sqrt(2.0f / (shininess + 2.0f)), 0.0f, 1.0f);
}

class ObjImporter final : public IImporter {
public:
    [[nodiscard]] std::string name() const override { return "obj"; }

    [[nodiscard]] std::vector<FormatInfo> formats() const override {
        return {{"obj", "Wavefront OBJ"}};
    }

    [[nodiscard]] int priority() const override { return 100; }

    [[nodiscard]] bool load(const std::filesystem::path& path, const ImportOptions& options,
                            scene::Scene& out, std::string& error) override;

private:
    /// Parses a companion .mtl file, appending to `out.materials`.
    void loadMaterialLibrary(const std::filesystem::path& path, scene::Scene& out,
                             std::unordered_map<std::string, int>& byName, ImageCache& cache);
};

void ObjImporter::loadMaterialLibrary(const std::filesystem::path& path, scene::Scene& out,
                                      std::unordered_map<std::string, int>& byName,
                                      ImageCache& cache) {
    std::vector<char> buffer;
    std::string error;
    if (!readFile(path, buffer, error)) {
        log::warn("mtl: {}", error);
        return;
    }

    const std::filesystem::path directory = path.parent_path();
    const char* p = buffer.data();
    const char* end = p + buffer.size();
    int current = -1;

    while (p < end) {
        skipSpaces(p, end);
        const std::string_view keyword = nextToken(p, end);

        if (keyword.empty() || keyword.front() == '#') {
            skipLine(p, end);
            continue;
        }

        auto readVec3 = [&](vec3& target) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (parseFloat(p, end, x) && parseFloat(p, end, y) && parseFloat(p, end, z)) {
                target = vec3(x, y, z);
            }
        };
        auto readFloat = [&](float& target) {
            float value = 0.0f;
            if (parseFloat(p, end, value)) target = value;
        };
        auto readTextureRef = [&]() -> std::string {
            // Skip map options such as "-bm 0.5" or "-s 1 1 1".
            std::string last;
            while (true) {
                const std::string_view token = nextToken(p, end);
                if (token.empty()) break;
                if (token.front() == '-') {
                    continue;  // option name; its values follow as plain tokens
                }
                last = std::string(token);
            }
            return last;
        };

        if (keyword == "newmtl") {
            const std::string_view materialName = nextToken(p, end);
            current = static_cast<int>(out.materials.size());
            scene::Material material;
            material.name = materialName.empty() ? "material" : std::string(materialName);
            out.materials.push_back(std::move(material));
            byName[out.materials.back().name] = current;
        } else if (current >= 0) {
            scene::Material& material = out.materials[static_cast<std::size_t>(current)];
            if (keyword == "Kd") {
                vec3 value(1.0f);
                readVec3(value);
                material.baseColor = vec4(value, material.baseColor.a);
            } else if (keyword == "Ke") {
                readVec3(material.emissive);
            } else if (keyword == "Ns") {
                float shininess = 0.0f;
                readFloat(shininess);
                material.roughness = shininessToRoughness(shininess);
            } else if (keyword == "Pr") {  // PBR extension
                readFloat(material.roughness);
            } else if (keyword == "Pm") {
                readFloat(material.metallic);
            } else if (keyword == "d") {
                float alpha = 1.0f;
                readFloat(alpha);
                material.baseColor.a = alpha;
                if (alpha < 0.999f) material.alphaMode = scene::AlphaMode::Blend;
            } else if (keyword == "Tr") {
                float transparency = 0.0f;
                readFloat(transparency);
                material.baseColor.a = 1.0f - transparency;
                if (transparency > 0.001f) material.alphaMode = scene::AlphaMode::Blend;
            } else if (keyword == "map_Kd") {
                material.baseColorTexture = loadTextureInto(out, directory, readTextureRef(), true, cache);
            } else if (keyword == "map_Ke") {
                material.emissiveTexture = loadTextureInto(out, directory, readTextureRef(), true, cache);
            } else if (keyword == "map_Bump" || keyword == "bump" || keyword == "norm") {
                material.normalTexture = loadTextureInto(out, directory, readTextureRef(), false, cache);
            } else if (keyword == "map_Pr" || keyword == "map_Ns") {
                material.metallicRoughnessTexture =
                    loadTextureInto(out, directory, readTextureRef(), false, cache);
            } else if (keyword == "map_d") {
                material.alphaMode = scene::AlphaMode::Mask;
            }
        }
        skipLine(p, end);
    }
}

bool ObjImporter::load(const std::filesystem::path& path, const ImportOptions& options,
                       scene::Scene& out, std::string& error) {
    std::vector<char> buffer;
    if (!readFile(path, buffer, error)) return false;
    if (buffer.empty()) {
        error = "file is empty";
        return false;
    }

    const std::filesystem::path directory = path.parent_path();
    ImageCache imageCache;
    std::unordered_map<std::string, int> materialsByName;

    std::vector<vec3> positions;
    std::vector<vec3> colors;  // parallel to positions; OBJ allows "v x y z r g b"
    std::vector<vec3> normals;
    std::vector<vec2> uvs;
    bool sawVertexColors = false;

    // Geometry accumulates into one bucket per (material, object) pair so we
    // can emit meshes that map 1:1 onto draw calls.
    struct Bucket {
        std::string name;
        int material = -1;
        std::vector<scene::Vertex> vertices;
        std::vector<std::uint32_t> indices;
        std::unordered_map<Corner, std::uint32_t, CornerHash> lookup;
    };
    std::vector<Bucket> buckets;
    std::unordered_map<std::string, std::size_t> bucketIndex;

    std::string currentObject = path.stem().string();
    int currentMaterial = -1;
    Bucket* bucket = nullptr;

    auto selectBucket = [&]() {
        const std::string key = currentObject + "\x1f" + std::to_string(currentMaterial);
        const auto it = bucketIndex.find(key);
        if (it != bucketIndex.end()) {
            bucket = &buckets[it->second];
            return;
        }
        bucketIndex[key] = buckets.size();
        Bucket fresh;
        fresh.name = currentObject;
        fresh.material = currentMaterial;
        buckets.push_back(std::move(fresh));
        bucket = &buckets.back();
    };

    const char* p = buffer.data();
    const char* end = p + buffer.size();
    std::vector<Corner> face;
    face.reserve(16);

    while (p < end) {
        skipSpaces(p, end);
        if (p >= end) break;

        const char c0 = *p;
        if (c0 == '#' || c0 == '\n') {
            skipLine(p, end);
            continue;
        }

        const std::string_view keyword = nextToken(p, end);

        if (keyword == "v") {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!parseFloat(p, end, x) || !parseFloat(p, end, y) || !parseFloat(p, end, z)) {
                skipLine(p, end);
                continue;
            }
            positions.emplace_back(x, y, z);

            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            if (parseFloat(p, end, r) && parseFloat(p, end, g) && parseFloat(p, end, b)) {
                colors.emplace_back(r, g, b);
                sawVertexColors = true;
            } else {
                colors.emplace_back(1.0f, 1.0f, 1.0f);
            }
        } else if (keyword == "vn") {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (parseFloat(p, end, x) && parseFloat(p, end, y) && parseFloat(p, end, z)) {
                normals.emplace_back(x, y, z);
            }
        } else if (keyword == "vt") {
            float u = 0.0f;
            float v = 0.0f;
            if (parseFloat(p, end, u)) {
                parseFloat(p, end, v);
                uvs.emplace_back(u, options.flipUVs ? 1.0f - v : v);
            }
        } else if (keyword == "f") {
            face.clear();
            while (true) {
                skipSpaces(p, end);
                if (p >= end || *p == '\n') break;

                Corner corner;
                long value = 0;
                if (!parseInt(p, end, value)) break;
                corner.position = resolveIndex(value, positions.size());

                if (p < end && *p == '/') {
                    ++p;
                    if (p < end && *p != '/') {
                        if (parseInt(p, end, value)) corner.uv = resolveIndex(value, uvs.size());
                    }
                    if (p < end && *p == '/') {
                        ++p;
                        if (parseInt(p, end, value)) {
                            corner.normal = resolveIndex(value, normals.size());
                        }
                    }
                }
                face.push_back(corner);
            }

            if (face.size() >= 3) {
                if (!bucket) selectBucket();

                auto emit = [&](const Corner& corner) -> std::uint32_t {
                    const auto it = bucket->lookup.find(corner);
                    if (it != bucket->lookup.end()) return it->second;

                    scene::Vertex vertex;
                    if (corner.position >= 0 &&
                        corner.position < static_cast<int>(positions.size())) {
                        vertex.position = positions[static_cast<std::size_t>(corner.position)];
                        if (sawVertexColors) {
                            vertex.color =
                                vec4(colors[static_cast<std::size_t>(corner.position)], 1.0f);
                        }
                    }
                    if (corner.normal >= 0 && corner.normal < static_cast<int>(normals.size())) {
                        vertex.normal = normals[static_cast<std::size_t>(corner.normal)];
                    } else {
                        vertex.normal = vec3(0.0f);  // signals "generate later"
                    }
                    if (corner.uv >= 0 && corner.uv < static_cast<int>(uvs.size())) {
                        vertex.uv = uvs[static_cast<std::size_t>(corner.uv)];
                    }

                    const auto index = static_cast<std::uint32_t>(bucket->vertices.size());
                    bucket->vertices.push_back(vertex);
                    bucket->lookup.emplace(corner, index);
                    return index;
                };

                // Fan triangulation: correct for the convex polygons OBJ files
                // realistically contain.
                const std::uint32_t first = emit(face[0]);
                std::uint32_t previous = emit(face[1]);
                for (std::size_t i = 2; i < face.size(); ++i) {
                    const std::uint32_t current = emit(face[i]);
                    bucket->indices.push_back(first);
                    bucket->indices.push_back(previous);
                    bucket->indices.push_back(current);
                    previous = current;
                }
            }
        } else if (keyword == "mtllib") {
            skipSpaces(p, end);
            const char* lineStart = p;
            while (p < end && *p != '\n' && *p != '\r') ++p;
            std::string libraries(lineStart, static_cast<std::size_t>(p - lineStart));
            // A single mtllib line may list several files.
            std::size_t offset = 0;
            while (offset < libraries.size()) {
                const std::size_t space = libraries.find(' ', offset);
                std::string entry = libraries.substr(offset, space - offset);
                if (!entry.empty()) {
                    const std::filesystem::path libraryPath =
                        std::filesystem::path(entry).is_absolute() ? std::filesystem::path(entry)
                                                                   : directory / entry;
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(libraryPath, ec)) {
                        loadMaterialLibrary(libraryPath, out, materialsByName, imageCache);
                    } else {
                        log::warn("mtllib not found: {}", entry);
                    }
                }
                if (space == std::string::npos) break;
                offset = space + 1;
            }
        } else if (keyword == "usemtl") {
            const std::string_view materialName = nextToken(p, end);
            const auto it = materialsByName.find(std::string(materialName));
            currentMaterial = it != materialsByName.end() ? it->second : -1;
            bucket = nullptr;
        } else if (keyword == "o" || keyword == "g") {
            const std::string_view objectName = nextToken(p, end);
            if (!objectName.empty()) currentObject = std::string(objectName);
            bucket = nullptr;
        }

        skipLine(p, end);
    }

    if (buckets.empty()) {
        error = "no faces found";
        return false;
    }

    const int root = out.ensureRoot();
    for (Bucket& source : buckets) {
        if (source.vertices.empty() || source.indices.empty()) continue;
        scene::Mesh mesh;
        mesh.name = source.name;
        mesh.vertices = std::move(source.vertices);
        mesh.indices = std::move(source.indices);
        mesh.material = source.material;
        const int meshIndex = static_cast<int>(out.meshes.size());
        out.meshes.push_back(std::move(mesh));
        out.nodes[static_cast<std::size_t>(root)].meshes.push_back(meshIndex);
    }

    if (out.meshes.empty()) {
        error = "no drawable geometry";
        return false;
    }
    return true;
}

}  // namespace

ImporterPtr makeObjImporter() { return std::make_unique<ObjImporter>(); }

}  // namespace tessera::io
