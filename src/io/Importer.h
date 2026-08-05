#pragma once

#include "scene/Scene.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tessera::io {

/// One file format advertised by an importer or exporter.
struct FormatInfo {
    std::string extension;    ///< lower-case, no dot, e.g. "gltf"
    std::string description;  ///< human readable, e.g. "glTF 2.0"
};

struct ImportOptions {
    bool generateNormals = true;
    bool generateTangents = true;
    bool flipUVs = false;
    bool joinIdenticalVertices = true;
    bool optimizeMeshes = false;
    float smoothingAngleDegrees = 66.0f;
    /// Uniformly scale the scene so its longest side equals this. <= 0 disables.
    float normalizeScale = 0.0f;
};

/// Plugin interface for reading a 3D file into a `scene::Scene`.
///
/// Adding a format means implementing this once and registering it with
/// `ImporterRegistry`; no other file in the project needs to change.
class IImporter {
public:
    virtual ~IImporter() = default;

    /// Short backend name shown in the UI, e.g. "obj" or "assimp".
    [[nodiscard]] virtual std::string name() const = 0;

    /// Extensions this importer handles. May be computed at runtime.
    [[nodiscard]] virtual std::vector<FormatInfo> formats() const = 0;

    /// Higher wins when several importers claim the same extension. Native
    /// fast paths use 100; the general-purpose fallback uses 0.
    [[nodiscard]] virtual int priority() const { return 0; }

    /// Reads `path` into `out`. Returns false and fills `error` on failure.
    /// The caller runs `Scene::finalize()`, so implementations only need to
    /// fill in the data they actually have.
    [[nodiscard]] virtual bool load(const std::filesystem::path& path, const ImportOptions& options,
                                    scene::Scene& out, std::string& error) = 0;
};

using ImporterPtr = std::unique_ptr<IImporter>;

}  // namespace tessera::io
