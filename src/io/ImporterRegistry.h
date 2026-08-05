#pragma once

#include "io/Importer.h"

#include <string_view>

namespace tessera::io {

/// Extension -> importer lookup. Importers register themselves here and the
/// rest of the program only ever talks to the registry.
class ImporterRegistry {
public:
    static ImporterRegistry& instance();

    void add(ImporterPtr importer);

    /// Importers claiming `extension`, best first.
    [[nodiscard]] std::vector<IImporter*> candidatesFor(std::string_view extension) const;

    [[nodiscard]] bool canLoad(const std::filesystem::path& path) const;

    /// Tries each candidate in priority order and keeps the first success.
    /// On total failure `error` explains what every candidate reported.
    [[nodiscard]] bool load(const std::filesystem::path& path, const ImportOptions& options,
                            scene::Scene& out, std::string& error) const;

    /// Every supported extension, de-duplicated and alphabetically sorted.
    [[nodiscard]] std::vector<FormatInfo> supportedFormats() const;

    [[nodiscard]] const std::vector<ImporterPtr>& importers() const { return importers_; }

private:
    ImporterRegistry() = default;
    std::vector<ImporterPtr> importers_;
};

/// Installs every importer compiled into this build. Safe to call more than
/// once; only the first call has an effect.
void registerBuiltinImporters();

}  // namespace tessera::io
