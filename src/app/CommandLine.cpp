#include "app/CommandLine.h"

#include "gfx/RenderBackend.h"
#include "io/FileUtil.h"
#include "io/ImporterRegistry.h"

#include <charconv>
#include <cstdio>
#include <string_view>

namespace tessera::app {
namespace {

bool parseNumber(std::string_view text, int& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parseNumber(std::string_view text, float& value) {
    // strtof rather than from_chars: floating-point from_chars is still missing
    // from some standard libraries we want to build against.
    const std::string copy(text);
    char* stop = nullptr;
    value = std::strtof(copy.c_str(), &stop);
    return stop != copy.c_str() && *stop == '\0';
}

/// "1920x1080" -> (1920, 1080)
bool parseSize(std::string_view text, int& width, int& height) {
    const std::size_t separator = text.find_first_of("xX*");
    if (separator == std::string_view::npos) return false;
    return parseNumber(text.substr(0, separator), width) &&
           parseNumber(text.substr(separator + 1), height);
}

/// "#rrggbb" or "rrggbb"
bool parseColor(std::string_view text, vec3& color) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6) return false;
    unsigned int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{}) return false;
    color = vec3(static_cast<float>((value >> 16) & 0xFF), static_cast<float>((value >> 8) & 0xFF),
                 static_cast<float>(value & 0xFF)) /
            255.0f;
    return true;
}

bool parseShadingMode(std::string_view text, gfx::ShadingMode& mode) {
    const std::string lowered = io::toLower(text);
    if (lowered == "shaded" || lowered == "pbr") mode = gfx::ShadingMode::Shaded;
    else if (lowered == "clay") mode = gfx::ShadingMode::Clay;
    else if (lowered == "albedo" || lowered == "basecolor") mode = gfx::ShadingMode::BaseColor;
    else if (lowered == "normals") mode = gfx::ShadingMode::Normals;
    else if (lowered == "tangents") mode = gfx::ShadingMode::Tangents;
    else if (lowered == "uv") mode = gfx::ShadingMode::Uv;
    else if (lowered == "metallic") mode = gfx::ShadingMode::Metallic;
    else if (lowered == "roughness") mode = gfx::ShadingMode::Roughness;
    else if (lowered == "occlusion" || lowered == "ao") mode = gfx::ShadingMode::Occlusion;
    else if (lowered == "vertexcolor" || lowered == "vcol") mode = gfx::ShadingMode::VertexColor;
    else return false;
    return true;
}

}  // namespace

Options parseCommandLine(int argc, char** argv) {
    Options options;

    const auto fail = [&options](std::string message) {
        options.valid = false;
        options.error = std::move(message);
    };

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);

        // Anything that is not a flag is the input file.
        if (argument.empty() || argument.front() != '-') {
            if (options.input.empty()) {
                options.input = argument;
            } else {
                fail(std::string("unexpected extra argument: ").append(argument));
                return options;
            }
            continue;
        }

        /// Fetches the value for an option that takes one.
        const auto value = [&](std::string_view name) -> std::string_view {
            if (i + 1 >= argc) {
                fail(std::string(name).append(" needs a value"));
                return {};
            }
            return argv[++i];
        };

        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
        } else if (argument == "--version") {
            options.showVersion = true;
        } else if (argument == "--formats") {
            options.listFormats = true;
        } else if (argument == "--export-formats") {
            options.listExportFormats = true;
        } else if (argument == "--list-backends") {
            options.listBackends = true;
        } else if (argument == "--backend") {
            options.backend = value(argument);
        } else if (argument == "-v" || argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "-q" || argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "-o" || argument == "--convert") {
            options.convertOutput = value(argument);
        } else if (argument == "-r" || argument == "--render") {
            options.renderOutput = value(argument);
        } else if (argument == "-s" || argument == "--size") {
            const std::string_view size = value(argument);
            if (!options.valid) return options;
            if (!parseSize(size, options.width, options.height)) {
                fail(std::string("expected WIDTHxHEIGHT, got: ").append(size));
                return options;
            }
        } else if (argument == "--samples") {
            const std::string_view samples = value(argument);
            if (!options.valid) return options;
            if (!parseNumber(samples, options.samples)) {
                fail(std::string("expected a number for --samples, got: ").append(samples));
                return options;
            }
        } else if (argument == "--no-normals") {
            options.import.generateNormals = false;
        } else if (argument == "--no-tangents") {
            options.import.generateTangents = false;
        } else if (argument == "--flip-uv") {
            options.import.flipUVs = true;
        } else if (argument == "--no-join") {
            options.import.joinIdenticalVertices = false;
        } else if (argument == "--optimize") {
            options.import.optimizeMeshes = true;
        } else if (argument == "--normalize") {
            const std::string_view size = value(argument);
            if (!options.valid) return options;
            if (!parseNumber(size, options.import.normalizeScale)) {
                fail(std::string("expected a number for --normalize, got: ").append(size));
                return options;
            }
        } else if (argument == "--ascii") {
            options.exportOptions.binary = false;
        } else if (argument == "--shading") {
            const std::string_view mode = value(argument);
            if (!options.valid) return options;
            if (!parseShadingMode(mode, options.render.shading)) {
                fail(std::string("unknown shading mode: ").append(mode));
                return options;
            }
        } else if (argument == "--wireframe") {
            options.render.wireframe = true;
        } else if (argument == "--no-grid") {
            options.render.showGrid = false;
        } else if (argument == "--bounds") {
            options.render.showBounds = true;
        } else if (argument == "--transparent") {
            options.render.showBackground = false;
        } else if (argument == "--bg") {
            const std::string_view color = value(argument);
            if (!options.valid) return options;
            vec3 parsed(0.0f);
            if (!parseColor(color, parsed)) {
                fail(std::string("expected a #rrggbb colour, got: ").append(color));
                return options;
            }
            options.render.backgroundTop = parsed;
            options.render.backgroundBottom = parsed;
        } else {
            fail(std::string("unknown option: ").append(argument));
            return options;
        }

        if (!options.valid) return options;
    }

    if (options.headless() && options.input.empty()) {
        fail("--convert and --render need an input file");
    }
    return options;
}

void printUsage() {
    std::printf(R"(tessera - a minimal, modular 3D model viewer

Usage:
  tessera [file] [options]

Modes:
  (no output option)     open the interactive viewer
  -o, --convert <file>   convert the input to another format and exit
  -r, --render <file>    render a PNG without opening a window and exit

Options:
      --backend NAME     render backend: opengl, vulkan, metal, optix, cuda
                         (see --list-backends for what this build supports)
  -s, --size WxH         viewport / output size            (default 1280x800)
      --samples N        multisample count                 (default 4)
      --shading MODE     shaded, clay, albedo, normals, tangents, uv,
                         metallic, roughness, ao, vertexcolor
      --wireframe        overlay the wireframe
      --bounds           overlay bounding boxes
      --no-grid          hide the ground grid
      --bg #rrggbb       flat background colour
      --transparent      no background (alpha 0 outside the model)

Import:
      --no-normals       keep whatever normals the file has, generate none
      --no-tangents      skip tangent generation
      --flip-uv          flip V, for files authored with the other convention
      --no-join          keep duplicate vertices
      --optimize         merge meshes and simplify the node graph
      --normalize N      uniformly scale the scene so its longest side is N

Export:
      --ascii            write the ASCII variant where the format has one

Info:
      --formats          list every readable format
      --export-formats   list every writable format
      --list-backends    list render backends and whether this machine can run them
  -v, --verbose          verbose logging
  -q, --quiet            errors only
      --version          print the version
  -h, --help             this text

Examples:
  tessera model.glb
  tessera scan.ply --shading vertexcolor
  tessera part.step -o part.stl --ascii
  tessera model.fbx -r thumb.png -s 512x512 --transparent
)");
}

void printFormats() {
    io::registerBuiltinImporters();
    const auto formats = io::ImporterRegistry::instance().supportedFormats();
    std::printf("%zu readable formats:\n", formats.size());
    for (const io::FormatInfo& format : formats) {
        std::printf("  .%-12s %s\n", format.extension.c_str(), format.description.c_str());
    }
}

void printExportFormats() {
    io::registerBuiltinExporters();
    const auto formats = io::ExporterRegistry::instance().supportedFormats();
    std::printf("%zu writable formats:\n", formats.size());
    for (const io::FormatInfo& format : formats) {
        std::printf("  .%-12s %s\n", format.extension.c_str(), format.description.c_str());
    }
}

void printBackends() {
    const auto backends = gfx::BackendRegistry::instance().all();
    const std::string defaultKey = gfx::BackendRegistry::instance().defaultKey();

    std::printf("Render backends:\n");
    for (const gfx::BackendInfo& backend : backends) {
        const char* state = !backend.compiledIn ? "not built"
                            : backend.available ? "ready"
                                                : "unavailable";
        std::printf("  %-8s %-11s %-30s %s%s\n", backend.key.c_str(), state,
                    backend.displayName.c_str(), backend.status.c_str(),
                    backend.key == defaultKey ? "  [default]" : "");
    }
}

}  // namespace tessera::app
