#pragma once

#include "scene/Scene.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace tessera::io {

/// Caches decoded images per scene so a texture shared by many materials is
/// only decoded once.
using ImageCache = std::unordered_map<std::string, int>;

bool decodeImage(const std::filesystem::path& path, bool srgb, scene::Image& out,
                 std::string& error);

bool decodeImageFromMemory(const void* data, std::size_t size, std::string_view name, bool srgb,
                           scene::Image& out, std::string& error);

/// Resolves a texture reference that may be absolute, relative to the model,
/// Windows-style, or just a file name that lives in a sibling directory.
/// Returns an empty path when nothing matches.
std::filesystem::path resolveTexturePath(const std::filesystem::path& modelDirectory,
                                         std::string_view reference);

/// Decodes `reference` and appends it to `scene.images`, returning its index
/// (or -1 if it could not be resolved/decoded).
int loadTextureInto(scene::Scene& scene, const std::filesystem::path& modelDirectory,
                    std::string_view reference, bool srgb, ImageCache& cache);

/// Writes an RGB/RGBA buffer to a PNG file.
bool writePng(const std::filesystem::path& path, int width, int height, int channels,
              const std::uint8_t* pixels, bool flipVertically, std::string& error);

}  // namespace tessera::io
