# Platform and toolchain configuration shared by every target.

# ---------------------------------------------------------------------------
# Compiler floor.
#
# The codebase uses std::format and other C++20 library features, which landed
# late in every standard library. Failing here with an explicit message beats
# a thousand lines of template errors later.
# ---------------------------------------------------------------------------
set(TESSERA_MIN_GCC   13)
set(TESSERA_MIN_CLANG 16)
set(TESSERA_MIN_APPLECLANG 15)
set(TESSERA_MIN_MSVC  19.29)  # Visual Studio 2019 16.10

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${TESSERA_MIN_GCC})
        message(FATAL_ERROR
            "GCC ${TESSERA_MIN_GCC} or newer is required for <format> support "
            "(found ${CMAKE_CXX_COMPILER_VERSION}). Install g++-${TESSERA_MIN_GCC}, "
            "or configure with -DCMAKE_CXX_COMPILER=clang++.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${TESSERA_MIN_APPLECLANG})
        message(FATAL_ERROR
            "Apple Clang ${TESSERA_MIN_APPLECLANG} or newer is required "
            "(found ${CMAKE_CXX_COMPILER_VERSION}). Update the Command Line Tools.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${TESSERA_MIN_CLANG})
        message(FATAL_ERROR
            "Clang ${TESSERA_MIN_CLANG} or newer is required "
            "(found ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()
elseif(MSVC)
    if(MSVC_VERSION LESS 1929)
        message(FATAL_ERROR "Visual Studio 2019 16.10 or newer is required.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Per-platform settings.
# ---------------------------------------------------------------------------
if(APPLE)
    # 13.3, not 11.0, and the reason is subtle: libc++ keeps the floating-point
    # std::to_chars in the shared library rather than the headers, annotated
    # with an availability attribute. std::format("{:.3f}", x) calls it, so any
    # lower deployment target fails to compile every float format string in the
    # project. (Floating-point std::from_chars is gated even harder, at macOS
    # 26, which is why the ASCII parsers use their own float scanner.)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version" FORCE)
    endif()

    # Both, and in this order. CMake routes .m files to OBJCXX when OBJCXX is
    # the only Objective-C dialect enabled, and GLFW's Cocoa sources are plain
    # Objective-C that does not compile under C++ conversion rules (a const
    # void* no longer converts implicitly). Enabling OBJC keeps .m on the C
    # front end; OBJCXX is what the Metal backend's .mm file needs.
    enable_language(OBJC)
    enable_language(OBJCXX)

    # Linux and Windows below: the code is written against their real
    # constraints, but nothing here has been compiled on either yet. Treat those
    # branches as unverified until CI actually exercises them.
elseif(UNIX)
    # GLVND is the modern OpenGL dispatch on Linux; without this CMake warns and
    # may pick the legacy libGL.
    set(OpenGL_GL_PREFERENCE GLVND)
    find_package(Threads REQUIRED)

elseif(WIN32)
    add_compile_definitions(
        NOMINMAX                  # <windows.h> min/max macros break std::min/max
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        UNICODE
        _UNICODE)
endif()

# ---------------------------------------------------------------------------
# Apple's ld64 warns when the same static archive reaches the link line through
# two paths, which is unavoidable for a library several targets share. GNU ld
# and link.exe stay quiet about it, so silence it here rather than distorting
# the dependency graph to work around a cosmetic diagnostic.
# ---------------------------------------------------------------------------
set(TESSERA_EXTRA_LINK_OPTIONS "")
if(APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries" TESSERA_LD_NO_WARN_DUPLICATES)
    if(TESSERA_LD_NO_WARN_DUPLICATES)
        list(APPEND TESSERA_EXTRA_LINK_OPTIONS "LINKER:-no_warn_duplicate_libraries")
    endif()
endif()

# ---------------------------------------------------------------------------
# Warning and codegen flags, applied through a helper so every target agrees.
# ---------------------------------------------------------------------------
function(tessera_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /utf-8          # source and execution charset, needed for the shader literals
            /permissive-    # standards conformance
            /bigobj         # the shader strings and templates push past the default limit
            /Zc:preprocessor
            /wd4201         # nameless struct/union, used throughout glm
        )
        if(TESSERA_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wno-unused-parameter
        )
        if(TESSERA_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()

    if(APPLE)
        # Apple's OpenGL headers are marked deprecated but remain the only
        # option; silence the noise rather than every call site.
        target_compile_definitions(${target} PRIVATE GL_SILENCE_DEPRECATION)
    endif()

    if(UNIX AND NOT APPLE)
        target_link_libraries(${target} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    endif()

    if(TESSERA_EXTRA_LINK_OPTIONS)
        target_link_options(${target} PRIVATE ${TESSERA_EXTRA_LINK_OPTIONS})
    endif()
endfunction()
