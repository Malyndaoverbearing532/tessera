#include "gfx/RenderBackend.h"

#include "io/FileUtil.h"

#include <format>

namespace tessera::gfx {

// Factories provided by whichever backend translation units are compiled in.
#if defined(TESSERA_BACKEND_OPENGL)
BackendPtr makeOpenGLBackend();
#endif
#if defined(TESSERA_BACKEND_VULKAN)
BackendPtr makeVulkanBackend();
#endif
#if defined(TESSERA_BACKEND_METAL)
BackendPtr makeMetalBackend();
#endif
#if defined(TESSERA_BACKEND_OPTIX)
BackendPtr makeOptixBackend();
#endif
#if defined(TESSERA_BACKEND_CUDA)
BackendPtr makeCudaBackend();
#endif

namespace {

/// The full catalogue, independent of what this build contains, so `--list-backends`
/// can distinguish "not compiled in" from "does not exist".
struct Entry {
    BackendId id;
    const char* key;
    const char* displayName;
    bool compiledIn;
    const char* absentReason;
};

constexpr Entry kCatalogue[] = {
    {BackendId::OpenGL, "opengl", "OpenGL 3.3 core",
#if defined(TESSERA_BACKEND_OPENGL)
     true,
#else
     false,
#endif
     "rebuild with -DTESSERA_BACKEND_OPENGL=ON"},

    {BackendId::Vulkan, "vulkan", "Vulkan 1.2",
#if defined(TESSERA_BACKEND_VULKAN)
     true,
#else
     false,
#endif
     "rebuild with -DTESSERA_BACKEND_VULKAN=ON (needs the LunarG SDK; MoltenVK on macOS)"},

    {BackendId::Metal, "metal", "Metal",
#if defined(TESSERA_BACKEND_METAL)
     true,
#else
     false,
#endif
     "Apple platforms only; rebuild with -DTESSERA_BACKEND_METAL=ON"},

    {BackendId::Optix, "optix", "NVIDIA OptiX (path tracing)",
#if defined(TESSERA_BACKEND_OPTIX)
     true,
#else
     false,
#endif
     "needs an NVIDIA GPU and the OptiX SDK; rebuild with -DTESSERA_BACKEND_OPTIX=ON "
     "-DTESSERA_OPTIX_ROOT=<sdk>"},

    {BackendId::Cuda, "cuda", "CUDA (compute rasteriser)",
#if defined(TESSERA_BACKEND_CUDA)
     true,
#else
     false,
#endif
     "needs an NVIDIA GPU and the CUDA toolkit; rebuild with -DTESSERA_BACKEND_CUDA=ON"},
};

}  // namespace

const char* shadingModeName(ShadingMode mode) {
    switch (mode) {
        case ShadingMode::Shaded: return "Shaded (PBR)";
        case ShadingMode::Clay: return "Clay";
        case ShadingMode::BaseColor: return "Base colour";
        case ShadingMode::Normals: return "Normals";
        case ShadingMode::Tangents: return "Tangents";
        case ShadingMode::Uv: return "UV";
        case ShadingMode::Metallic: return "Metallic";
        case ShadingMode::Roughness: return "Roughness";
        case ShadingMode::Occlusion: return "Occlusion";
        case ShadingMode::VertexColor: return "Vertex colour";
        case ShadingMode::Count: break;
    }
    return "?";
}

BackendRegistry& BackendRegistry::instance() {
    static BackendRegistry registry;
    return registry;
}

std::vector<BackendInfo> BackendRegistry::all() const {
    std::vector<BackendInfo> result;
    result.reserve(std::size(kCatalogue));

    for (const Entry& entry : kCatalogue) {
        BackendInfo info;
        info.id = entry.id;
        info.key = entry.key;
        info.displayName = entry.displayName;
        info.compiledIn = entry.compiledIn;

        if (!entry.compiledIn) {
            info.available = false;
            info.status = entry.absentReason;
        } else {
            // Ask the backend itself; only it knows whether the device is there.
            std::string error;
            if (BackendPtr backend = create(entry.key, error)) {
                const BackendInfo live = backend->info();
                info.available = live.available;
                info.status = live.status;
            } else {
                info.available = false;
                info.status = error;
            }
        }
        result.push_back(std::move(info));
    }
    return result;
}

std::string BackendRegistry::defaultKey() const {
    // First compiled-in and usable entry wins; the catalogue order is the
    // preference order.
    for (const BackendInfo& info : all()) {
        if (info.compiledIn && info.available) return info.key;
    }
    for (const Entry& entry : kCatalogue) {
        if (entry.compiledIn) return entry.key;
    }
    return "opengl";
}

BackendPtr BackendRegistry::create(std::string_view key, std::string& error) const {
    const std::string wanted = io::toLower(key);

#if defined(TESSERA_BACKEND_OPENGL)
    if (wanted == "opengl" || wanted == "gl") return makeOpenGLBackend();
#endif
#if defined(TESSERA_BACKEND_VULKAN)
    if (wanted == "vulkan" || wanted == "vk") return makeVulkanBackend();
#endif
#if defined(TESSERA_BACKEND_METAL)
    if (wanted == "metal") return makeMetalBackend();
#endif
#if defined(TESSERA_BACKEND_OPTIX)
    if (wanted == "optix") return makeOptixBackend();
#endif
#if defined(TESSERA_BACKEND_CUDA)
    if (wanted == "cuda") return makeCudaBackend();
#endif

    for (const Entry& entry : kCatalogue) {
        if (wanted == entry.key) {
            error = std::format("backend '{}' is not part of this build: {}", wanted,
                                entry.absentReason);
            return nullptr;
        }
    }
    error = std::format("unknown backend '{}' (try --list-backends)", wanted);
    return nullptr;
}

}  // namespace tessera::gfx
