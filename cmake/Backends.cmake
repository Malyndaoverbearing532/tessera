# Render backend selection.
#
# Each backend is opt-in and independently detected. A backend that is asked
# for but whose SDK is missing produces a warning and is switched off rather
# than breaking the build, so a checkout always configures somewhere.
#
# Only backends compiled in appear in `tessera --list-backends`; the rest are
# reported as "not compiled in" so the list is never misleading.

option(TESSERA_BACKEND_OPENGL "Build the OpenGL 3.3 backend"            ON)
option(TESSERA_BACKEND_VULKAN "Build the Vulkan backend"                OFF)
option(TESSERA_BACKEND_METAL  "Build the Metal backend (Apple only)"    OFF)
option(TESSERA_BACKEND_OPTIX  "Build the NVIDIA OptiX backend"          OFF)
option(TESSERA_BACKEND_CUDA   "Build the CUDA backend"                  OFF)

set(TESSERA_OPTIX_ROOT "" CACHE PATH
    "OptiX SDK root. The SDK is behind NVIDIA's developer login and cannot be fetched automatically.")

set(TESSERA_ENABLED_BACKENDS "")

# ---------------------------------------------------------------------------
# OpenGL - the reference backend. Portable to all three desktop platforms.
# ---------------------------------------------------------------------------
if(TESSERA_BACKEND_OPENGL)
    find_package(OpenGL REQUIRED)
    list(APPEND TESSERA_ENABLED_BACKENDS opengl)
endif()

# ---------------------------------------------------------------------------
# Vulkan - portable, but on Apple it runs through MoltenVK rather than natively.
# ---------------------------------------------------------------------------
if(TESSERA_BACKEND_VULKAN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        list(APPEND TESSERA_ENABLED_BACKENDS vulkan)
        message(STATUS "Vulkan found: ${Vulkan_LIBRARY}")
        if(APPLE)
            message(STATUS "  on macOS this requires MoltenVK from the LunarG SDK at runtime")
        endif()
    else()
        message(WARNING
            "TESSERA_BACKEND_VULKAN was requested but no Vulkan SDK was found. "
            "Install the LunarG SDK and set VULKAN_SDK. Disabling the backend.")
        set(TESSERA_BACKEND_VULKAN OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# Metal - Apple only.
#
# The offline `metal` shader compiler ships with Xcode, not with the Command
# Line Tools, so the backend compiles its shaders at runtime from MSL source
# instead. That keeps a full Xcode install from being a build requirement.
# ---------------------------------------------------------------------------
if(TESSERA_BACKEND_METAL)
    if(NOT APPLE)
        message(WARNING "TESSERA_BACKEND_METAL only applies to Apple platforms. Disabling.")
        set(TESSERA_BACKEND_METAL OFF)
    else()
        find_library(TESSERA_METAL_FRAMEWORK Metal)
        find_library(TESSERA_QUARTZCORE_FRAMEWORK QuartzCore)
        find_library(TESSERA_FOUNDATION_FRAMEWORK Foundation)
        if(TESSERA_METAL_FRAMEWORK AND TESSERA_QUARTZCORE_FRAMEWORK AND TESSERA_FOUNDATION_FRAMEWORK)
            list(APPEND TESSERA_ENABLED_BACKENDS metal)

            # Report whether an offline shader toolchain exists, purely so the
            # configure log tells the truth about which path will be used.
            execute_process(COMMAND xcrun -sdk macosx -f metal
                            RESULT_VARIABLE TESSERA_METAL_TOOL_RESULT
                            OUTPUT_QUIET ERROR_QUIET)
            if(TESSERA_METAL_TOOL_RESULT EQUAL 0)
                message(STATUS "Metal: offline shader compiler available")
            else()
                message(STATUS "Metal: no offline compiler (Command Line Tools only); "
                               "shaders will be built at runtime from MSL source")
            endif()
        else()
            message(WARNING "Metal frameworks not found. Disabling the backend.")
            set(TESSERA_BACKEND_METAL OFF)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# CUDA / OptiX - NVIDIA hardware only, and therefore Linux or Windows only.
# Apple has shipped no CUDA driver since macOS 10.13, so these are hard-disabled
# there instead of failing later at runtime.
# ---------------------------------------------------------------------------
if(TESSERA_BACKEND_CUDA OR TESSERA_BACKEND_OPTIX)
    if(APPLE)
        message(WARNING
            "CUDA and OptiX are unavailable on macOS: NVIDIA has shipped no macOS driver "
            "since CUDA 10.2 (2019). Disabling both backends.")
        set(TESSERA_BACKEND_CUDA OFF)
        set(TESSERA_BACKEND_OPTIX OFF)
    else()
        find_package(CUDAToolkit QUIET)
        if(NOT CUDAToolkit_FOUND)
            message(WARNING
                "CUDA toolkit not found; disabling the CUDA and OptiX backends. "
                "Install the CUDA Toolkit and ensure nvcc is on PATH.")
            set(TESSERA_BACKEND_CUDA OFF)
            set(TESSERA_BACKEND_OPTIX OFF)
        else()
            enable_language(CUDA)
            message(STATUS "CUDA toolkit ${CUDAToolkit_VERSION} found")

            if(TESSERA_BACKEND_CUDA)
                list(APPEND TESSERA_ENABLED_BACKENDS cuda)
            endif()

            if(TESSERA_BACKEND_OPTIX)
                # OptiX is header-only at build time but needs the SDK, which is
                # login-walled and so can never be fetched automatically.
                find_path(TESSERA_OPTIX_INCLUDE_DIR optix.h
                    HINTS ${TESSERA_OPTIX_ROOT} $ENV{OPTIX_ROOT} $ENV{OptiX_INSTALL_DIR}
                    PATH_SUFFIXES include)
                if(TESSERA_OPTIX_INCLUDE_DIR)
                    message(STATUS "OptiX headers: ${TESSERA_OPTIX_INCLUDE_DIR}")
                    list(APPEND TESSERA_ENABLED_BACKENDS optix)
                else()
                    message(WARNING
                        "OptiX headers not found. Download the SDK from NVIDIA and pass "
                        "-DTESSERA_OPTIX_ROOT=/path/to/OptiX. Disabling the backend.")
                    set(TESSERA_BACKEND_OPTIX OFF)
                endif()
            endif()
        endif()
    endif()
endif()

if(NOT TESSERA_ENABLED_BACKENDS)
    message(FATAL_ERROR "No render backend is enabled. At minimum keep TESSERA_BACKEND_OPENGL on.")
endif()

list(JOIN TESSERA_ENABLED_BACKENDS ", " TESSERA_ENABLED_BACKENDS_TEXT)
