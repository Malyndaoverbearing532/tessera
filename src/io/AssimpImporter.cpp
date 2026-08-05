// General-purpose backend built on Assimp.
//
// It advertises whatever extension list the linked Assimp build reports, minus
// the ones a native fast path already covers better. Because it registers at
// priority 0 it only runs when no specialised importer claims the file - or
// when one of them declines an exotic variant.

#include "core/Log.h"
#include "io/FileUtil.h"
#include "io/ImageLoader.h"
#include "io/Importer.h"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <format>
#include <sstream>
#include <unordered_map>

namespace tessera::io {
namespace {

vec3 toVec3(const aiVector3D& v) { return {v.x, v.y, v.z}; }
vec4 toVec4(const aiColor4D& c) { return {c.r, c.g, c.b, c.a}; }

mat4 toMat4(const aiMatrix4x4& m) {
    // Assimp is row-major, glm is column-major.
    return {m.a1, m.b1, m.c1, m.d1,
            m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3,
            m.a4, m.b4, m.c4, m.d4};
}

/// Extensions handled by a dedicated importer in this project. Assimp still
/// lists them so it can act as a fallback, but they are hidden from the format
/// summary to keep the UI honest about which backend runs.
bool hasNativeFastPath(std::string_view extension) {
    return extension == "obj" || extension == "stl" || extension == "ply";
}

class AssimpImporter final : public IImporter {
public:
    [[nodiscard]] std::string name() const override { return "assimp"; }

    [[nodiscard]] std::vector<FormatInfo> formats() const override;

    [[nodiscard]] int priority() const override { return 0; }

    [[nodiscard]] bool load(const std::filesystem::path& path, const ImportOptions& options,
                            scene::Scene& out, std::string& error) override;

private:
    void convertMaterials(const aiScene& source, const std::filesystem::path& directory,
                          scene::Scene& out);
    void convertMeshes(const aiScene& source, scene::Scene& out);
    int convertNode(const aiNode& node, int parent, scene::Scene& out);

    /// Assimp reports embedded textures as "*0"; those are decoded from memory.
    int resolveTexture(const aiScene& source, const std::filesystem::path& directory,
                       const aiString& reference, bool srgb, scene::Scene& out);

    ImageCache cache_;
    std::unordered_map<std::string, int> embeddedCache_;
};

std::vector<FormatInfo> AssimpImporter::formats() const {
    // Query the linked library instead of hard-coding a list, so upgrading
    // Assimp automatically widens format support.
    static const std::vector<FormatInfo> cached = [] {
        Assimp::Importer importer;
        aiString list;
        importer.GetExtensionList(list);

        std::vector<FormatInfo> result;
        std::stringstream stream(list.C_Str());
        std::string token;
        while (std::getline(stream, token, ';')) {
            // Tokens look like "*.dae".
            const std::size_t dot = token.find_last_of('.');
            if (dot == std::string::npos) continue;
            std::string extension = toLower(token.substr(dot + 1));
            if (extension.empty()) continue;
            result.push_back({extension, "via Assimp"});
        }
        return result;
    }();
    return cached;
}

int AssimpImporter::resolveTexture(const aiScene& source, const std::filesystem::path& directory,
                                   const aiString& reference, bool srgb, scene::Scene& out) {
    const std::string_view text(reference.C_Str(), reference.length);
    if (text.empty()) return -1;

    if (text.front() == '*') {
        const std::string key = std::string(text) + (srgb ? "|s" : "|l");
        if (const auto it = embeddedCache_.find(key); it != embeddedCache_.end()) return it->second;

        const aiTexture* texture = source.GetEmbeddedTexture(reference.C_Str());
        if (!texture) return -1;

        scene::Image image;
        std::string error;
        bool ok = false;
        if (texture->mHeight == 0) {
            // Compressed blob (PNG/JPEG bytes).
            ok = decodeImageFromMemory(texture->pcData, texture->mWidth, text, srgb, image, error);
        } else {
            // Raw ARGB8888.
            image.name = std::string(text);
            image.width = static_cast<int>(texture->mWidth);
            image.height = static_cast<int>(texture->mHeight);
            image.channels = 4;
            image.srgb = srgb;
            image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4);
            for (std::size_t i = 0; i < image.pixels.size() / 4; ++i) {
                image.pixels[i * 4 + 0] = texture->pcData[i].r;
                image.pixels[i * 4 + 1] = texture->pcData[i].g;
                image.pixels[i * 4 + 2] = texture->pcData[i].b;
                image.pixels[i * 4 + 3] = texture->pcData[i].a;
            }
            ok = true;
        }

        if (!ok) {
            log::warn("{}", error);
            embeddedCache_[key] = -1;
            return -1;
        }
        const int index = static_cast<int>(out.images.size());
        out.images.push_back(std::move(image));
        embeddedCache_[key] = index;
        return index;
    }

    return loadTextureInto(out, directory, text, srgb, cache_);
}

void AssimpImporter::convertMaterials(const aiScene& source, const std::filesystem::path& directory,
                                      scene::Scene& out) {
    out.materials.reserve(source.mNumMaterials);

    for (unsigned int i = 0; i < source.mNumMaterials; ++i) {
        const aiMaterial& src = *source.mMaterials[i];
        scene::Material material;

        aiString name;
        if (src.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0) {
            material.name = name.C_Str();
        } else {
            material.name = std::format("material_{}", i);
        }

        aiColor4D color;
        if (src.Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS) {
            material.baseColor = toVec4(color);
        } else if (src.Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            material.baseColor = toVec4(color);
        }

        aiColor4D emissive;
        if (src.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
            material.emissive = vec3(emissive.r, emissive.g, emissive.b);
        }

        float value = 0.0f;
        if (src.Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS) material.metallic = value;
        if (src.Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS) {
            material.roughness = value;
        } else if (src.Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS && value > 0.0f) {
            material.roughness = std::clamp(std::sqrt(2.0f / (value + 2.0f)), 0.0f, 1.0f);
        }

        float opacity = 1.0f;
        if (src.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity < 0.999f) {
            material.baseColor.a *= opacity;
            material.alphaMode = scene::AlphaMode::Blend;
        }

        int twoSided = 0;
        if (src.Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
            material.doubleSided = twoSided != 0;
        }

        aiString gltfAlphaMode;
        if (src.Get(AI_MATKEY_GLTF_ALPHAMODE, gltfAlphaMode) == AI_SUCCESS) {
            const std::string_view mode(gltfAlphaMode.C_Str());
            if (mode == "BLEND") material.alphaMode = scene::AlphaMode::Blend;
            else if (mode == "MASK") material.alphaMode = scene::AlphaMode::Mask;
            else material.alphaMode = scene::AlphaMode::Opaque;
        }
        if (src.Get(AI_MATKEY_GLTF_ALPHACUTOFF, value) == AI_SUCCESS) material.alphaCutoff = value;

        // Texture slots. sRGB for colour data, linear for data maps.
        aiString texturePath;
        auto tryTexture = [&](aiTextureType type, unsigned int index, bool srgb) -> int {
            if (src.GetTexture(type, index, &texturePath) != AI_SUCCESS) return -1;
            return resolveTexture(source, directory, texturePath, srgb, out);
        };

        material.baseColorTexture = tryTexture(aiTextureType_BASE_COLOR, 0, true);
        if (material.baseColorTexture < 0) material.baseColorTexture = tryTexture(aiTextureType_DIFFUSE, 0, true);

        material.metallicRoughnessTexture = tryTexture(aiTextureType_METALNESS, 0, false);
        if (material.metallicRoughnessTexture < 0) {
            material.metallicRoughnessTexture = tryTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, false);
        }
        if (material.metallicRoughnessTexture < 0) {
            material.metallicRoughnessTexture = tryTexture(aiTextureType_UNKNOWN, 0, false);
        }

        material.normalTexture = tryTexture(aiTextureType_NORMALS, 0, false);
        if (material.normalTexture < 0) material.normalTexture = tryTexture(aiTextureType_HEIGHT, 0, false);

        material.emissiveTexture = tryTexture(aiTextureType_EMISSIVE, 0, true);
        material.occlusionTexture = tryTexture(aiTextureType_AMBIENT_OCCLUSION, 0, false);
        if (material.occlusionTexture < 0) material.occlusionTexture = tryTexture(aiTextureType_LIGHTMAP, 0, false);

        out.materials.push_back(std::move(material));
    }
}

void AssimpImporter::convertMeshes(const aiScene& source, scene::Scene& out) {
    out.meshes.reserve(source.mNumMeshes);

    for (unsigned int m = 0; m < source.mNumMeshes; ++m) {
        const aiMesh& src = *source.mMeshes[m];
        scene::Mesh mesh;
        mesh.name = src.mName.length > 0 ? src.mName.C_Str() : std::format("mesh_{}", m);
        mesh.material = static_cast<int>(src.mMaterialIndex);

        if (src.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) mesh.topology = scene::Topology::Triangles;
        else if (src.mPrimitiveTypes & aiPrimitiveType_LINE) mesh.topology = scene::Topology::Lines;
        else if (src.mPrimitiveTypes & aiPrimitiveType_POINT) mesh.topology = scene::Topology::Points;

        mesh.vertices.resize(src.mNumVertices);
        const bool hasNormals = src.HasNormals();
        const bool hasTangents = src.HasTangentsAndBitangents();
        const bool hasUVs = src.HasTextureCoords(0);
        const bool hasColors = src.HasVertexColors(0);

        for (unsigned int v = 0; v < src.mNumVertices; ++v) {
            scene::Vertex& vertex = mesh.vertices[v];
            vertex.position = toVec3(src.mVertices[v]);
            vertex.normal = hasNormals ? toVec3(src.mNormals[v]) : vec3(0.0f);
            if (hasTangents) {
                const vec3 tangent = toVec3(src.mTangents[v]);
                const vec3 bitangent = toVec3(src.mBitangents[v]);
                const float handedness =
                    glm::dot(glm::cross(vertex.normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                vertex.tangent = vec4(tangent, handedness);
            }
            if (hasUVs) vertex.uv = vec2(src.mTextureCoords[0][v].x, src.mTextureCoords[0][v].y);
            if (hasColors) vertex.color = toVec4(src.mColors[0][v]);
        }

        mesh.indices.reserve(static_cast<std::size_t>(src.mNumFaces) * 3);
        for (unsigned int f = 0; f < src.mNumFaces; ++f) {
            const aiFace& face = src.mFaces[f];
            if (face.mNumIndices < 2) {
                if (mesh.topology == scene::Topology::Points && face.mNumIndices == 1) {
                    mesh.indices.push_back(face.mIndices[0]);
                }
                continue;
            }
            if (face.mNumIndices == 2) {
                mesh.indices.push_back(face.mIndices[0]);
                mesh.indices.push_back(face.mIndices[1]);
                continue;
            }
            for (unsigned int k = 2; k < face.mNumIndices; ++k) {
                mesh.indices.push_back(face.mIndices[0]);
                mesh.indices.push_back(face.mIndices[k - 1]);
                mesh.indices.push_back(face.mIndices[k]);
            }
        }

        out.meshes.push_back(std::move(mesh));
    }
}

int AssimpImporter::convertNode(const aiNode& node, int parent, scene::Scene& out) {
    const std::string name = node.mName.length > 0 ? node.mName.C_Str() : "node";
    const int index = out.addNode(name, parent, toMat4(node.mTransformation));

    for (unsigned int i = 0; i < node.mNumMeshes; ++i) {
        out.nodes[static_cast<std::size_t>(index)].meshes.push_back(
            static_cast<int>(node.mMeshes[i]));
    }
    for (unsigned int i = 0; i < node.mNumChildren; ++i) {
        convertNode(*node.mChildren[i], index, out);
    }
    return index;
}

bool AssimpImporter::load(const std::filesystem::path& path, const ImportOptions& options,
                          scene::Scene& out, std::string& error) {
    cache_.clear();
    embeddedCache_.clear();

    Assimp::Importer importer;
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, options.smoothingAngleDegrees);
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
                                aiPrimitiveType_LINE | aiPrimitiveType_POINT);

    unsigned int flags = aiProcess_Triangulate | aiProcess_SortByPType |
                         aiProcess_ImproveCacheLocality | aiProcess_FindInvalidData |
                         aiProcess_GenUVCoords | aiProcess_LimitBoneWeights;
    if (options.generateNormals) flags |= aiProcess_GenSmoothNormals;
    if (options.generateTangents) flags |= aiProcess_CalcTangentSpace;
    if (options.joinIdenticalVertices) flags |= aiProcess_JoinIdenticalVertices;
    if (options.flipUVs) flags |= aiProcess_FlipUVs;
    if (options.optimizeMeshes) flags |= aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph;

    const aiScene* source = importer.ReadFile(path.string(), flags);
    if (!source || !source->mRootNode) {
        error = importer.GetErrorString();
        if (error.empty()) error = "assimp returned no scene";
        return false;
    }
    if (source->mNumMeshes == 0) {
        error = "file contains no meshes";
        return false;
    }

    const std::filesystem::path directory = path.parent_path();
    convertMaterials(*source, directory, out);
    convertMeshes(*source, out);
    convertNode(*source->mRootNode, -1, out);

    if (source->mAnimations != nullptr && source->mNumAnimations > 0) {
        log::info("note: '{}' contains {} animation(s); this build renders the bind pose",
                  path.filename().string(), source->mNumAnimations);
    }
    return true;
}

}  // namespace

ImporterPtr makeAssimpImporter() { return std::make_unique<AssimpImporter>(); }

/// Extensions Assimp claims that a native importer already handles.
bool assimpExtensionHasNativeFastPath(std::string_view extension) {
    return hasNativeFastPath(extension);
}

}  // namespace tessera::io
