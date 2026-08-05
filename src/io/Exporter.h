#pragma once

#include "io/Importer.h"

namespace tessera::io {

struct ExportOptions {
    /// Bake node transforms into vertex positions. Formats without a scene
    /// graph (STL, PLY) always do this regardless of the flag.
    bool flattenHierarchy = false;
    bool binary = true;  ///< for formats offering both, e.g. STL and PLY
};

/// Writes a scene out. Mirrors `IImporter` so backends stay symmetrical.
class IExporter {
public:
    virtual ~IExporter() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::vector<FormatInfo> formats() const = 0;
    [[nodiscard]] virtual int priority() const { return 0; }

    [[nodiscard]] virtual bool save(const scene::Scene& source, const std::filesystem::path& path,
                                    const ExportOptions& options, std::string& error) = 0;
};

using ExporterPtr = std::unique_ptr<IExporter>;

class ExporterRegistry {
public:
    static ExporterRegistry& instance();

    void add(ExporterPtr exporter);

    [[nodiscard]] std::vector<FormatInfo> supportedFormats() const;

    [[nodiscard]] bool save(const scene::Scene& source, const std::filesystem::path& path,
                            const ExportOptions& options, std::string& error) const;

private:
    ExporterRegistry() = default;
    std::vector<ExporterPtr> exporters_;
};

void registerBuiltinExporters();

}  // namespace tessera::io
