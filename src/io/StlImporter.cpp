// STL reader (binary and ASCII).
//
// STL is a triangle soup with per-facet normals, so this importer keeps the
// facet normals rather than smoothing them - that is what makes CAD exports
// look right. The optional VisCAM/SolidWorks colour extension in the attribute
// byte count is honoured when present.

#include "core/Log.h"
#include "io/FileUtil.h"
#include "io/Importer.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <unordered_map>

namespace tessera::io {
namespace {

constexpr std::size_t kBinaryHeaderSize = 80;
constexpr std::size_t kBinaryTriangleSize = 50;  // 12 floats + uint16

float readFloatLE(const char* p) {
    float value = 0.0f;
    std::memcpy(&value, p, sizeof(float));
    return value;
}

std::uint32_t readUint32LE(const char* p) {
    std::uint32_t value = 0;
    std::memcpy(&value, p, sizeof(std::uint32_t));
    return value;
}

std::uint16_t readUint16LE(const char* p) {
    std::uint16_t value = 0;
    std::memcpy(&value, p, sizeof(std::uint16_t));
    return value;
}

struct VertexKey {
    vec3 position;
    vec3 normal;
    vec4 color;

    bool operator==(const VertexKey& other) const {
        return position == other.position && normal == other.normal && color == other.color;
    }
};

struct VertexKeyHash {
    std::size_t operator()(const VertexKey& key) const noexcept {
        std::size_t h = 1469598103934665603ULL;
        auto mix = [&h](float f) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            h ^= bits;
            h *= 1099511628211ULL;
        };
        mix(key.position.x);
        mix(key.position.y);
        mix(key.position.z);
        mix(key.normal.x);
        mix(key.normal.y);
        mix(key.normal.z);
        return h;
    }
};

/// Accumulates triangles, optionally welding exact duplicates.
class TriangleBuilder {
public:
    explicit TriangleBuilder(bool weld) : weld_(weld) {}

    void add(const vec3& a, const vec3& b, const vec3& c, const vec3& normal, const vec4& color) {
        vec3 n = normal;
        if (glm::length(n) < 1e-8f) {
            // Some writers emit a zero normal and expect the reader to derive it.
            const vec3 derived = glm::cross(b - a, c - a);
            n = glm::length(derived) > 1e-12f ? glm::normalize(derived) : vec3(0.0f, 1.0f, 0.0f);
        } else {
            n = glm::normalize(n);
        }
        for (const vec3& position : {a, b, c}) push(VertexKey{position, n, color});
    }

    std::vector<scene::Vertex> vertices;
    std::vector<std::uint32_t> indices;

private:
    void push(const VertexKey& key) {
        if (weld_) {
            const auto it = lookup_.find(key);
            if (it != lookup_.end()) {
                indices.push_back(it->second);
                return;
            }
        }
        const auto index = static_cast<std::uint32_t>(vertices.size());
        scene::Vertex vertex;
        vertex.position = key.position;
        vertex.normal = key.normal;
        vertex.color = key.color;
        vertices.push_back(vertex);
        indices.push_back(index);
        if (weld_) lookup_.emplace(key, index);
    }

    bool weld_;
    std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> lookup_;
};

bool looksBinary(const std::vector<char>& buffer) {
    if (buffer.size() < kBinaryHeaderSize + 4) return false;

    const std::uint32_t count = readUint32LE(buffer.data() + kBinaryHeaderSize);
    const std::size_t expected = kBinaryHeaderSize + 4 + static_cast<std::size_t>(count) * kBinaryTriangleSize;
    // The size check is authoritative: some binary files start with "solid".
    if (expected == buffer.size()) return true;

    const std::string_view head(buffer.data(), std::min<std::size_t>(buffer.size(), 6));
    if (head.substr(0, 5) == "solid") return false;
    // Trailing junk is common; accept a binary file that is merely long enough.
    return expected <= buffer.size();
}

bool parseBinary(const std::vector<char>& buffer, bool weld, scene::Scene& out,
                 std::string& error) {
    const std::uint32_t count = readUint32LE(buffer.data() + kBinaryHeaderSize);
    const std::size_t required = kBinaryHeaderSize + 4 + static_cast<std::size_t>(count) * kBinaryTriangleSize;
    if (required > buffer.size()) {
        error = std::format("truncated binary STL: declares {} triangles but only {} bytes",
                            count, buffer.size());
        return false;
    }

    TriangleBuilder builder(weld);
    builder.vertices.reserve(static_cast<std::size_t>(count) * 3);
    builder.indices.reserve(static_cast<std::size_t>(count) * 3);

    const char* p = buffer.data() + kBinaryHeaderSize + 4;
    bool sawColor = false;
    for (std::uint32_t i = 0; i < count; ++i, p += kBinaryTriangleSize) {
        const vec3 normal(readFloatLE(p), readFloatLE(p + 4), readFloatLE(p + 8));
        const vec3 a(readFloatLE(p + 12), readFloatLE(p + 16), readFloatLE(p + 20));
        const vec3 b(readFloatLE(p + 24), readFloatLE(p + 28), readFloatLE(p + 32));
        const vec3 c(readFloatLE(p + 36), readFloatLE(p + 40), readFloatLE(p + 44));

        vec4 color(1.0f);
        const std::uint16_t attribute = readUint16LE(p + 48);
        if (attribute & 0x8000u) {  // VisCAM / SolidWorks 5-bit-per-channel colour
            color = vec4(static_cast<float>((attribute >> 10) & 0x1Fu) / 31.0f,
                         static_cast<float>((attribute >> 5) & 0x1Fu) / 31.0f,
                         static_cast<float>(attribute & 0x1Fu) / 31.0f, 1.0f);
            sawColor = true;
        }
        builder.add(a, b, c, normal, color);
    }

    if (builder.indices.empty()) {
        error = "binary STL contains no triangles";
        return false;
    }

    scene::Material material;
    material.name = "stl";
    material.roughness = 0.55f;
    material.metallic = 0.0f;
    material.baseColor = vec4(0.78f, 0.79f, 0.82f, 1.0f);
    out.materials.push_back(material);

    scene::Mesh mesh;
    mesh.name = "stl";
    mesh.vertices = std::move(builder.vertices);
    mesh.indices = std::move(builder.indices);
    mesh.material = 0;
    out.meshes.push_back(std::move(mesh));

    if (sawColor) log::trace("STL contains per-facet colours");
    return true;
}

bool parseAscii(const std::vector<char>& buffer, bool weld, scene::Scene& out, std::string& error) {
    TriangleBuilder builder(weld);

    const char* p = buffer.data();
    const char* end = p + buffer.size();
    vec3 normal(0.0f);
    vec3 corner[3];
    int cornerCount = 0;

    while (p < end) {
        skipSpaces(p, end);
        const std::string_view keyword = nextToken(p, end);

        if (keyword == "facet") {
            const std::string_view maybeNormal = nextToken(p, end);
            normal = vec3(0.0f);
            if (maybeNormal == "normal") {
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                if (parseFloat(p, end, x) && parseFloat(p, end, y) && parseFloat(p, end, z)) {
                    normal = vec3(x, y, z);
                }
            }
            cornerCount = 0;
        } else if (keyword == "vertex") {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (parseFloat(p, end, x) && parseFloat(p, end, y) && parseFloat(p, end, z) &&
                cornerCount < 3) {
                corner[cornerCount++] = vec3(x, y, z);
            }
        } else if (keyword == "endfacet") {
            if (cornerCount == 3) {
                builder.add(corner[0], corner[1], corner[2], normal, vec4(1.0f));
            }
            cornerCount = 0;
        }
        skipLine(p, end);
    }

    if (builder.indices.empty()) {
        error = "ASCII STL contains no triangles";
        return false;
    }

    scene::Material material;
    material.name = "stl";
    material.roughness = 0.55f;
    material.baseColor = vec4(0.78f, 0.79f, 0.82f, 1.0f);
    out.materials.push_back(material);

    scene::Mesh mesh;
    mesh.name = "stl";
    mesh.vertices = std::move(builder.vertices);
    mesh.indices = std::move(builder.indices);
    mesh.material = 0;
    out.meshes.push_back(std::move(mesh));
    return true;
}

class StlImporter final : public IImporter {
public:
    [[nodiscard]] std::string name() const override { return "stl"; }

    [[nodiscard]] std::vector<FormatInfo> formats() const override {
        return {{"stl", "Stereolithography (binary + ASCII)"}};
    }

    [[nodiscard]] int priority() const override { return 100; }

    [[nodiscard]] bool load(const std::filesystem::path& path, const ImportOptions& options,
                            scene::Scene& out, std::string& error) override {
        std::vector<char> buffer;
        if (!readFile(path, buffer, error)) return false;
        if (buffer.size() < 15) {
            error = "file is too small to be an STL";
            return false;
        }

        const bool ok = looksBinary(buffer)
                            ? parseBinary(buffer, options.joinIdenticalVertices, out, error)
                            : parseAscii(buffer, options.joinIdenticalVertices, out, error);
        if (!ok) return false;

        out.ensureRoot();
        out.nodes[0].meshes.push_back(0);
        return true;
    }
};

}  // namespace

ImporterPtr makeStlImporter() { return std::make_unique<StlImporter>(); }

}  // namespace tessera::io
