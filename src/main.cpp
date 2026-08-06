#include "app/Application.h"
#include "app/CommandLine.h"
#include "core/Log.h"

#include <cstdio>

int main(int argc, char** argv) {
    tessera::app::Options options = tessera::app::parseCommandLine(argc, argv);

    if (!options.valid) {
        std::fprintf(stderr, "tessera: %s\nTry 'tessera --help'.\n", options.error.c_str());
        return 2;
    }

    if (options.verbose) tessera::log::setLevel(tessera::log::Level::Trace);
    if (options.quiet) tessera::log::setLevel(tessera::log::Level::Error);

    if (options.showHelp) {
        tessera::app::printUsage();
        return 0;
    }
    if (options.showVersion) {
        std::printf("tessera %s\n", TESSERA_VERSION);
        return 0;
    }
    if (options.listFormats) {
        tessera::app::printFormats();
        return 0;
    }
    if (options.listExportFormats) {
        tessera::app::printExportFormats();
        return 0;
    }
    if (options.listBackends) {
        tessera::app::printBackends();
        return 0;
    }

    if (!options.convertOutput.empty()) {
        return tessera::app::Application::runConvert(options);
    }
    if (!options.renderOutput.empty()) {
        return tessera::app::Application::runHeadlessRender(options);
    }
    if (options.benchmarkFrames > 0) {
        return tessera::app::Application::runBenchmark(options);
    }

    tessera::app::Application application;
    return application.run(options);
}
