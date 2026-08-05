#include "io/ImageLoader.h"

#include "core/Log.h"
#include "io/FileUtil.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <format>

namespace tessera::io {
namespace {

void fillFromStb(scene::Image& out, unsigned char* data, int width, int height, int channels,
                 std::string_view name, bool srgb) {
    out.name = std::string(name);
    out.width = width;
    out.height = height;
    out.channels = channels;
    out.srgb = srgb;
    const std::size_t bytes = static_cast<std::size_t>(width) * height * channels;
    out.pixels.assign(data, data + bytes);
}

}  // namespace

bool decodeImage(const std::filesystem::path& path, bool srgb, scene::Image& out,
                 std::string& error) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);
    if (!data) {
        error = std::format("{}: {}", path.filename().string(), stbi_failure_reason());
        return false;
    }
    fillFromStb(out, data, width, height, channels, path.filename().string(), srgb);
    stbi_image_free(data);
    return true;
}

bool decodeImageFromMemory(const void* data, std::size_t size, std::string_view name, bool srgb,
                           scene::Image& out, std::string& error) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data),
                                                  static_cast<int>(size), &width, &height,
                                                  &channels, 0);
    if (!pixels) {
        error = std::format("embedded image '{}': {}", name, stbi_failure_reason());
        return false;
    }
    fillFromStb(out, pixels, width, height, channels, name, srgb);
    stbi_image_free(pixels);
    return true;
}

std::filesystem::path resolveTexturePath(const std::filesystem::path& modelDirectory,
                                         std::string_view reference) {
    if (reference.empty()) return {};

    // Normalise Windows separators, which show up constantly in OBJ/MTL/FBX.
    std::string normalized(reference);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (!normalized.empty() && (normalized.front() == ' ' || normalized.front() == '"')) {
        normalized.erase(normalized.begin());
    }
    while (!normalized.empty() && (normalized.back() == ' ' || normalized.back() == '"')) {
        normalized.pop_back();
    }
    if (normalized.empty()) return {};

    const std::filesystem::path raw(normalized);
    std::error_code ec;

    std::vector<std::filesystem::path> candidates = {
        raw,
        modelDirectory / raw,
        modelDirectory / raw.filename(),
        modelDirectory / "textures" / raw.filename(),
        modelDirectory / "Textures" / raw.filename(),
        modelDirectory / "maps" / raw.filename(),
        modelDirectory.parent_path() / raw,
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate, ec)) return candidate;
    }

    // Last resort: case-insensitive match on the file name in the model folder.
    const std::string wanted = toLower(raw.filename().string());
    if (std::filesystem::is_directory(modelDirectory, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(modelDirectory, ec)) {
            if (!entry.is_regular_file()) continue;
            if (toLower(entry.path().filename().string()) == wanted) return entry.path();
        }
    }
    return {};
}

int loadTextureInto(scene::Scene& scene, const std::filesystem::path& modelDirectory,
                    std::string_view reference, bool srgb, ImageCache& cache) {
    if (reference.empty()) return -1;

    const std::string key = std::string(reference) + (srgb ? "|s" : "|l");
    if (const auto it = cache.find(key); it != cache.end()) return it->second;

    const std::filesystem::path resolved = resolveTexturePath(modelDirectory, reference);
    if (resolved.empty()) {
        log::warn("texture not found: {}", reference);
        cache[key] = -1;
        return -1;
    }

    scene::Image image;
    std::string error;
    if (!decodeImage(resolved, srgb, image, error)) {
        log::warn("{}", error);
        cache[key] = -1;
        return -1;
    }

    const int index = static_cast<int>(scene.images.size());
    scene.images.push_back(std::move(image));
    cache[key] = index;
    return index;
}

bool writePng(const std::filesystem::path& path, int width, int height, int channels,
              const std::uint8_t* pixels, bool flipVertically, std::string& error) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

    stbi_flip_vertically_on_write(flipVertically ? 1 : 0);
    const int ok = stbi_write_png(path.string().c_str(), width, height, channels, pixels,
                                  width * channels);
    stbi_flip_vertically_on_write(0);

    if (!ok) {
        error = std::format("failed to write '{}'", path.string());
        return false;
    }
    return true;
}

}  // namespace tessera::io
