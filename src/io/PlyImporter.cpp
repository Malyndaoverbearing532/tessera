// Stanford PLY reader: ASCII, binary little-endian and binary big-endian.
//
// Handles the property spellings that scanners and photogrammetry tools
// actually emit, and falls back to a point cloud when the file has no faces -
// which is the common case for LiDAR and Gaussian-splat exports.

#include "core/Log.h"
#include "io/FileUtil.h"
#include "io/Importer.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace tessera::io {
namespace {

enum class ScalarType { Int8, Uint8, Int16, Uint16, Int32, Uint32, Float32, Float64, Invalid };

std::size_t sizeOf(ScalarType type) {
    switch (type) {
        case ScalarType::Int8:
        case ScalarType::Uint8: return 1;
        case ScalarType::Int16:
        case ScalarType::Uint16: return 2;
        case ScalarType::Int32:
        case ScalarType::Uint32:
        case ScalarType::Float32: return 4;
        case ScalarType::Float64: return 8;
        case ScalarType::Invalid: return 0;
    }
    return 0;
}

ScalarType scalarFromName(std::string_view name) {
    if (name == "char" || name == "int8") return ScalarType::Int8;
    if (name == "uchar" || name == "uint8") return ScalarType::Uint8;
    if (name == "short" || name == "int16") return ScalarType::Int16;
    if (name == "ushort" || name == "uint16") return ScalarType::Uint16;
    if (name == "int" || name == "int32") return ScalarType::Int32;
    if (name == "uint" || name == "uint32") return ScalarType::Uint32;
    if (name == "float" || name == "float32") return ScalarType::Float32;
    if (name == "double" || name == "float64") return ScalarType::Float64;
    return ScalarType::Invalid;
}

struct Property {
    std::string name;
    ScalarType type = ScalarType::Invalid;
    bool isList = false;
    ScalarType countType = ScalarType::Invalid;
};

struct Element {
    std::string name;
    std::size_t count = 0;
    std::vector<Property> properties;
};

enum class Format { Ascii, BinaryLittleEndian, BinaryBigEndian };

/// Cursor over the binary body that knows how to byte-swap.
class BinaryReader {
public:
    BinaryReader(const char* begin, const char* end, bool swap)
        : p_(begin), end_(end), swap_(swap) {}

    [[nodiscard]] bool exhausted() const { return p_ >= end_; }

    bool read(ScalarType type, double& value) {
        const std::size_t size = sizeOf(type);
        if (size == 0 || static_cast<std::size_t>(end_ - p_) < size) return false;

        char raw[8];
        std::memcpy(raw, p_, size);
        if (swap_ && size > 1) std::reverse(raw, raw + size);
        p_ += size;

        switch (type) {
            case ScalarType::Int8: value = *reinterpret_cast<const std::int8_t*>(raw); break;
            case ScalarType::Uint8: value = *reinterpret_cast<const std::uint8_t*>(raw); break;
            case ScalarType::Int16: {
                std::int16_t v;
                std::memcpy(&v, raw, 2);
                value = v;
                break;
            }
            case ScalarType::Uint16: {
                std::uint16_t v;
                std::memcpy(&v, raw, 2);
                value = v;
                break;
            }
            case ScalarType::Int32: {
                std::int32_t v;
                std::memcpy(&v, raw, 4);
                value = v;
                break;
            }
            case ScalarType::Uint32: {
                std::uint32_t v;
                std::memcpy(&v, raw, 4);
                value = v;
                break;
            }
            case ScalarType::Float32: {
                float v;
                std::memcpy(&v, raw, 4);
                value = v;
                break;
            }
            case ScalarType::Float64: {
                double v;
                std::memcpy(&v, raw, 8);
                value = v;
                break;
            }
            case ScalarType::Invalid: return false;
        }
        return true;
    }

private:
    const char* p_;
    const char* end_;
    bool swap_;
};

/// Which vertex property index maps to which semantic slot.
struct VertexLayout {
    int x = -1, y = -1, z = -1;
    int nx = -1, ny = -1, nz = -1;
    int u = -1, v = -1;
    int red = -1, green = -1, blue = -1, alpha = -1;

    [[nodiscard]] bool hasPosition() const { return x >= 0 && y >= 0 && z >= 0; }
    [[nodiscard]] bool hasNormal() const { return nx >= 0 && ny >= 0 && nz >= 0; }
    [[nodiscard]] bool hasUv() const { return u >= 0 && v >= 0; }
    [[nodiscard]] bool hasColor() const { return red >= 0 && green >= 0 && blue >= 0; }
};

VertexLayout buildLayout(const Element& element) {
    VertexLayout layout;
    for (int i = 0; i < static_cast<int>(element.properties.size()); ++i) {
        const std::string& name = element.properties[static_cast<std::size_t>(i)].name;
        if (name == "x") layout.x = i;
        else if (name == "y") layout.y = i;
        else if (name == "z") layout.z = i;
        else if (name == "nx") layout.nx = i;
        else if (name == "ny") layout.ny = i;
        else if (name == "nz") layout.nz = i;
        else if (name == "s" || name == "u" || name == "texture_u" || name == "texture_s") layout.u = i;
        else if (name == "t" || name == "v" || name == "texture_v" || name == "texture_t") layout.v = i;
        else if (name == "red" || name == "r" || name == "diffuse_red") layout.red = i;
        else if (name == "green" || name == "g" || name == "diffuse_green") layout.green = i;
        else if (name == "blue" || name == "b" || name == "diffuse_blue") layout.blue = i;
        else if (name == "alpha" || name == "a") layout.alpha = i;
    }
    return layout;
}

/// Colour channels may be bytes (0-255) or floats (0-1); normalise both.
float normalizeChannel(double value, ScalarType type) {
    if (type == ScalarType::Float32 || type == ScalarType::Float64) {
        return static_cast<float>(std::clamp(value, 0.0, 1.0));
    }
    const double scale = (type == ScalarType::Uint16 || type == ScalarType::Int16) ? 65535.0 : 255.0;
    return static_cast<float>(std::clamp(value / scale, 0.0, 1.0));
}

bool isFaceProperty(std::string_view name) {
    return name == "vertex_indices" || name == "vertex_index" || name == "vertex-indices";
}

class PlyImporter final : public IImporter {
public:
    [[nodiscard]] std::string name() const override { return "ply"; }

    [[nodiscard]] std::vector<FormatInfo> formats() const override {
        return {{"ply", "Stanford Polygon / point cloud"}};
    }

    [[nodiscard]] int priority() const override { return 100; }

    [[nodiscard]] bool load(const std::filesystem::path& path, const ImportOptions& options,
                            scene::Scene& out, std::string& error) override;
};

bool PlyImporter::load(const std::filesystem::path& path, const ImportOptions& options,
                       scene::Scene& out, std::string& error) {
    std::vector<char> buffer;
    if (!readFile(path, buffer, error)) return false;
    if (buffer.size() < 4 || std::string_view(buffer.data(), 3) != "ply") {
        error = "missing 'ply' magic";
        return false;
    }

    // ------------------------------------------------------------------
    // Header
    // ------------------------------------------------------------------
    const char* p = buffer.data();
    const char* end = p + buffer.size();
    Format format = Format::Ascii;
    std::vector<Element> elements;
    bool sawFormat = false;
    bool headerClosed = false;

    skipLine(p, end);  // consume the magic
    while (p < end) {
        const char* lineStart = p;
        const std::string_view keyword = nextToken(p, end);

        if (keyword == "format") {
            const std::string_view formatName = nextToken(p, end);
            if (formatName == "ascii") format = Format::Ascii;
            else if (formatName == "binary_little_endian") format = Format::BinaryLittleEndian;
            else if (formatName == "binary_big_endian") format = Format::BinaryBigEndian;
            else {
                error = std::format("unknown PLY format '{}'", formatName);
                return false;
            }
            sawFormat = true;
        } else if (keyword == "element") {
            Element element;
            element.name = std::string(nextToken(p, end));
            long count = 0;
            parseInt(p, end, count);
            element.count = static_cast<std::size_t>(std::max(0L, count));
            elements.push_back(std::move(element));
        } else if (keyword == "property") {
            if (elements.empty()) {
                error = "property declared before any element";
                return false;
            }
            Property property;
            const std::string_view typeName = nextToken(p, end);
            if (typeName == "list") {
                property.isList = true;
                property.countType = scalarFromName(nextToken(p, end));
                property.type = scalarFromName(nextToken(p, end));
            } else {
                property.type = scalarFromName(typeName);
            }
            property.name = std::string(nextToken(p, end));
            if (property.type == ScalarType::Invalid) {
                error = std::format("unknown property type in '{}'", property.name);
                return false;
            }
            elements.back().properties.push_back(std::move(property));
        } else if (keyword == "end_header") {
            skipLine(p, end);
            headerClosed = true;
            break;
        }

        if (p == lineStart) ++p;  // guard against a zero-length token stalling us
        skipLine(p, end);
    }

    if (!sawFormat || !headerClosed) {
        error = "malformed PLY header";
        return false;
    }

    // ------------------------------------------------------------------
    // Body
    // ------------------------------------------------------------------
    std::vector<scene::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    bool hasNormals = false;
    bool hasColors = false;

    const bool binary = format != Format::Ascii;
    const bool bigEndian = format == Format::BinaryBigEndian;
    const bool swap = binary && (bigEndian != (std::endian::native == std::endian::big));
    BinaryReader reader(p, end, swap);

    // One shared value reader keeps the ASCII and binary paths identical below.
    auto readValue = [&](ScalarType type, double& value) -> bool {
        if (binary) return reader.read(type, value);
        float parsed = 0.0f;
        // Numbers may sit on the next line; step over newlines explicitly.
        while (p < end && (*p == '\n' || isSpace(*p))) ++p;
        if (!parseFloat(p, end, parsed)) return false;
        value = parsed;
        return true;
    };

    for (const Element& element : elements) {
        const bool isVertex = element.name == "vertex";
        const bool isFace = element.name == "face";
        const VertexLayout layout = isVertex ? buildLayout(element) : VertexLayout{};

        if (isVertex) {
            if (!layout.hasPosition()) {
                error = "vertex element has no x/y/z";
                return false;
            }
            vertices.resize(element.count);
            hasNormals = layout.hasNormal();
            hasColors = layout.hasColor();
        }

        std::vector<double> scalars(element.properties.size(), 0.0);
        std::vector<std::uint32_t> list;

        for (std::size_t i = 0; i < element.count; ++i) {
            list.clear();
            for (std::size_t pi = 0; pi < element.properties.size(); ++pi) {
                const Property& property = element.properties[pi];

                if (property.isList) {
                    double rawCount = 0.0;
                    if (!readValue(property.countType, rawCount)) {
                        error = std::format("truncated list in element '{}'", element.name);
                        return false;
                    }
                    const auto entries = static_cast<std::size_t>(std::max(0.0, rawCount));
                    for (std::size_t k = 0; k < entries; ++k) {
                        double value = 0.0;
                        if (!readValue(property.type, value)) {
                            error = std::format("truncated list data in element '{}'", element.name);
                            return false;
                        }
                        if (isFace && isFaceProperty(property.name)) {
                            list.push_back(static_cast<std::uint32_t>(std::max(0.0, value)));
                        }
                    }
                } else {
                    if (!readValue(property.type, scalars[pi])) {
                        error = std::format("truncated data in element '{}'", element.name);
                        return false;
                    }
                }
            }

            if (isVertex) {
                scene::Vertex& vertex = vertices[i];
                vertex.position = vec3(static_cast<float>(scalars[static_cast<std::size_t>(layout.x)]),
                                       static_cast<float>(scalars[static_cast<std::size_t>(layout.y)]),
                                       static_cast<float>(scalars[static_cast<std::size_t>(layout.z)]));
                if (layout.hasNormal()) {
                    vertex.normal = vec3(static_cast<float>(scalars[static_cast<std::size_t>(layout.nx)]),
                                         static_cast<float>(scalars[static_cast<std::size_t>(layout.ny)]),
                                         static_cast<float>(scalars[static_cast<std::size_t>(layout.nz)]));
                } else {
                    vertex.normal = vec3(0.0f);
                }
                if (layout.hasUv()) {
                    const float u = static_cast<float>(scalars[static_cast<std::size_t>(layout.u)]);
                    const float v = static_cast<float>(scalars[static_cast<std::size_t>(layout.v)]);
                    vertex.uv = vec2(u, options.flipUVs ? 1.0f - v : v);
                }
                if (layout.hasColor()) {
                    const auto channelType = [&](int index) {
                        return element.properties[static_cast<std::size_t>(index)].type;
                    };
                    vertex.color = vec4(
                        normalizeChannel(scalars[static_cast<std::size_t>(layout.red)], channelType(layout.red)),
                        normalizeChannel(scalars[static_cast<std::size_t>(layout.green)], channelType(layout.green)),
                        normalizeChannel(scalars[static_cast<std::size_t>(layout.blue)], channelType(layout.blue)),
                        layout.alpha >= 0 ? normalizeChannel(scalars[static_cast<std::size_t>(layout.alpha)],
                                                             channelType(layout.alpha))
                                          : 1.0f);
                }
            } else if (isFace && list.size() >= 3) {
                for (std::size_t k = 2; k < list.size(); ++k) {  // fan triangulation
                    indices.push_back(list[0]);
                    indices.push_back(list[k - 1]);
                    indices.push_back(list[k]);
                }
            }
        }
    }

    if (vertices.empty()) {
        error = "no vertices";
        return false;
    }

    // Drop indices that point outside the vertex array rather than crashing the
    // GPU path later.
    const auto vertexCount = static_cast<std::uint32_t>(vertices.size());
    std::size_t dropped = 0;
    for (std::size_t i = 0; i + 2 < indices.size();) {
        if (indices[i] >= vertexCount || indices[i + 1] >= vertexCount ||
            indices[i + 2] >= vertexCount) {
            indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i),
                          indices.begin() + static_cast<std::ptrdiff_t>(i + 3));
            ++dropped;
            continue;
        }
        i += 3;
    }
    if (dropped > 0) log::warn("PLY: dropped {} triangles with out-of-range indices", dropped);

    scene::Material material;
    material.name = "ply";
    material.roughness = 0.65f;
    material.baseColor = vec4(0.8f, 0.8f, 0.82f, 1.0f);
    out.materials.push_back(material);

    scene::Mesh mesh;
    mesh.name = path.stem().string();
    mesh.vertices = std::move(vertices);
    mesh.indices = std::move(indices);
    mesh.material = 0;
    // No faces means this is a point cloud, which the renderer draws as points.
    mesh.topology = mesh.indices.empty() ? scene::Topology::Points : scene::Topology::Triangles;
    out.meshes.push_back(std::move(mesh));

    out.ensureRoot();
    out.nodes[0].meshes.push_back(0);

    log::trace("PLY: {} vertices, normals={} colors={} topology={}", out.meshes[0].vertices.size(),
               hasNormals, hasColors,
               out.meshes[0].topology == scene::Topology::Points ? "points" : "triangles");
    return true;
}

}  // namespace

ImporterPtr makePlyImporter() { return std::make_unique<PlyImporter>(); }

}  // namespace tessera::io
