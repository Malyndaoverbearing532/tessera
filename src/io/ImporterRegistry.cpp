#include "io/ImporterRegistry.h"

#include "core/Log.h"
#include "io/FileUtil.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <unordered_set>

namespace tessera::io {

// Declared by the individual backend translation units.
ImporterPtr makeObjImporter();
ImporterPtr makeStlImporter();
ImporterPtr makePlyImporter();
#if defined(TESSERA_WITH_ASSIMP)
ImporterPtr makeAssimpImporter();
#endif

ImporterRegistry& ImporterRegistry::instance() {
    static ImporterRegistry registry;
    return registry;
}

void ImporterRegistry::add(ImporterPtr importer) {
    if (!importer) return;
    importers_.push_back(std::move(importer));
    std::stable_sort(importers_.begin(), importers_.end(),
                     [](const ImporterPtr& a, const ImporterPtr& b) {
                         return a->priority() > b->priority();
                     });
}

std::vector<IImporter*> ImporterRegistry::candidatesFor(std::string_view extension) const {
    const std::string ext = toLower(extension);
    std::vector<IImporter*> result;
    for (const ImporterPtr& importer : importers_) {
        for (const FormatInfo& format : importer->formats()) {
            if (format.extension == ext) {
                result.push_back(importer.get());
                break;
            }
        }
    }
    return result;  // importers_ is kept sorted, so this is already best-first
}

bool ImporterRegistry::canLoad(const std::filesystem::path& path) const {
    return !candidatesFor(extensionOf(path)).empty();
}

bool ImporterRegistry::load(const std::filesystem::path& path, const ImportOptions& options,
                            scene::Scene& out, std::string& error) const {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        error = std::format("no such file: {}", path.string());
        return false;
    }

    const std::string ext = extensionOf(path);
    const std::vector<IImporter*> candidates = candidatesFor(ext);
    if (candidates.empty()) {
        error = std::format("unsupported format '.{}' (run with --formats to list supported types)",
                            ext);
        return false;
    }

    const auto start = std::chrono::steady_clock::now();
    std::string attempts;

    for (IImporter* importer : candidates) {
        out.clear();
        std::string localError;
        if (importer->load(path, options, out, localError)) {
            out.sourcePath = path.string();
            out.importerName = importer->name();
            out.finalize(options.generateNormals, options.generateTangents);

            if (options.normalizeScale > 0.0f) {
                const Aabb box = out.bounds();
                const float longest = box.longestEdge();
                if (longest > 1e-9f) {
                    const float factor = options.normalizeScale / longest;
                    out.nodes[0].transform = glm::scale(out.nodes[0].transform, vec3(factor));
                }
            }

            const auto end = std::chrono::steady_clock::now();
            out.stats.loadSeconds = std::chrono::duration<double>(end - start).count();
            log::info("loaded '{}' via {} in {:.3f}s ({} meshes, {} tris)", path.filename().string(),
                      importer->name(), out.stats.loadSeconds, out.stats.meshCount,
                      out.stats.triangleCount);
            return true;
        }
        // Keep going: a native fast path may legitimately bail on an exotic
        // variant that the general-purpose backend still handles.
        log::trace("importer '{}' declined '{}': {}", importer->name(), path.string(), localError);
        attempts += std::format("\n  {}: {}", importer->name(), localError);
    }

    out.clear();
    error = std::format("failed to load '{}'{}", path.string(), attempts);
    return false;
}

std::vector<FormatInfo> ImporterRegistry::supportedFormats() const {
    std::vector<FormatInfo> result;
    std::unordered_set<std::string> seen;
    for (const ImporterPtr& importer : importers_) {
        for (const FormatInfo& format : importer->formats()) {
            if (seen.insert(format.extension).second) result.push_back(format);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const FormatInfo& a, const FormatInfo& b) { return a.extension < b.extension; });
    return result;
}

void registerBuiltinImporters() {
    static bool done = false;
    if (done) return;
    done = true;

    ImporterRegistry& registry = ImporterRegistry::instance();
    // Native fast paths first; the general-purpose backend is the fallback.
    registry.add(makeObjImporter());
    registry.add(makeStlImporter());
    registry.add(makePlyImporter());
#if defined(TESSERA_WITH_ASSIMP)
    registry.add(makeAssimpImporter());
#endif

    log::trace("{} importers registered, {} formats supported", registry.importers().size(),
               registry.supportedFormats().size());
}

}  // namespace tessera::io
