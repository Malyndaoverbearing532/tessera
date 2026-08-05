cmake_minimum_required(VERSION 3.21)

project(tessera
    VERSION 0.1.0
    DESCRIPTION "A minimal, modular 3D model viewer"
    HOMEPAGE_URL "https://github.com/"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Single-config generators need a default; multi-config ones ignore this.
get_property(TESSERA_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT TESSERA_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Keep the binary and any runtime DLLs side by side, which is what Windows needs.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR}/bin)
endforeach()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(TESSERA_WITH_ASSIMP "Enable the Assimp importer/exporter backend (~50 formats)" ON)
option(TESSERA_WITH_UI     "Build the Dear ImGui interface"                            ON)
option(TESSERA_WERROR      "Treat warnings as errors"                                  OFF)
option(TESSERA_INSTALL     "Generate install rules"                                    ON)

# Package-manager copies are convenient but are built against whatever the host
# runs, which on macOS means a Homebrew dylib can carry a newer minimum OS than
# the deployment target we ask for. Turn this on for redistributable builds so
# every dependency is compiled from a pinned source revision instead.
option(TESSERA_PREFER_BUNDLED_DEPS "Always build dependencies from source"             OFF)

# Off by default so development builds produce a plain binary you can run
# straight from build/bin. Required for the .dmg.
option(TESSERA_MACOS_BUNDLE "Build tessera as a macOS .app bundle"                         OFF)

if(NOT APPLE)
    message(WARNING
        "Tessera is only tested on macOS. The Linux and Windows build paths are "
        "written but have never been compiled, so expect to fix things. Reports "
        "and patches welcome.")
endif()

include(Platform)
include(Backends)
include(Dependencies)
include(Packaging)  # defines the bundling helpers used inside src/

add_subdirectory(src)

# Install and CPack rules need the target to exist first.
tessera_add_install_rules()

message(STATUS "")
message(STATUS "tessera ${PROJECT_VERSION}")
message(STATUS "  system ........... ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  compiler ......... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  build type ....... ${CMAKE_BUILD_TYPE}")
message(STATUS "  render backends .. ${TESSERA_ENABLED_BACKENDS_TEXT}")
message(STATUS "  assimp backend ... ${TESSERA_WITH_ASSIMP}")
message(STATUS "  imgui ui ......... ${TESSERA_WITH_UI}")
message(STATUS "")
