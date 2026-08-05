#pragma once

#include "gfx/RenderSettings.h"
#include "io/Exporter.h"
#include "io/Importer.h"

#include <filesystem>
#include <string>

namespace tessera::app {

/// Parsed command line. The viewer has three modes, chosen by which output
/// option is present: interactive (none), convert (--convert) and headless
/// render (--render).
struct Options {
    std::filesystem::path input;
    std::filesystem::path convertOutput;
    std::filesystem::path renderOutput;

    int width = 1280;
    int height = 800;
    int samples = 4;

    /// Render backend key ("opengl", "vulkan", ...). Empty means "pick one".
    std::string backend;

    bool listFormats = false;
    bool listExportFormats = false;
    bool listBackends = false;
    bool showHelp = false;
    bool showVersion = false;
    bool verbose = false;
    bool quiet = false;

    io::ImportOptions import;
    io::ExportOptions exportOptions;
    gfx::RenderSettings render;

    /// Set when parsing failed; `error` explains why.
    bool valid = true;
    std::string error;

    [[nodiscard]] bool headless() const { return !renderOutput.empty() || !convertOutput.empty(); }
};

Options parseCommandLine(int argc, char** argv);

void printUsage();
void printFormats();
void printExportFormats();
void printBackends();

}  // namespace tessera::app
