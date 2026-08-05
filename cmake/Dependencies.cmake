# Dependency resolution.
#
# Policy: prefer a system/package-manager copy when one is present (fast builds,
# distro-friendly), otherwise fetch a pinned source revision so the repository
# builds standalone with nothing but a compiler, CMake, Python and git.

include(FetchContent)

# ---------------------------------------------------------------------------
# glfw - windowing / input / drag & drop
# ---------------------------------------------------------------------------
if(NOT TESSERA_PREFER_BUNDLED_DEPS)
    find_package(glfw3 3.3 QUIET)
endif()
if(NOT glfw3_FOUND)
    message(STATUS "glfw3 not found, fetching source")
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    if(UNIX AND NOT APPLE)
        # Build both display backends so one binary runs under X11 and Wayland.
        set(GLFW_BUILD_X11     ON CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(glfw3
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glfw3)
endif()

# ---------------------------------------------------------------------------
# glm - math
# ---------------------------------------------------------------------------
if(NOT TESSERA_PREFER_BUNDLED_DEPS)
    find_package(glm QUIET)
endif()
if(NOT glm_FOUND)
    message(STATUS "glm not found, fetching source")
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glm)
endif()

# glm exports different target names depending on how it was installed.
if(TARGET glm::glm)
    set(TESSERA_GLM_TARGET glm::glm)
elseif(TARGET glm)
    set(TESSERA_GLM_TARGET glm)
else()
    message(FATAL_ERROR "glm was found but exports no usable target")
endif()

# ---------------------------------------------------------------------------
# glad - OpenGL 3.3 core loader.
#
# We only populate the sources and run the generator ourselves rather than
# adding glad's own CMakeLists: it declares cmake_minimum_required(VERSION 3.0),
# which CMake 4 refuses outright. Driving the generator directly also lets us
# pass --reproducible, so the loader is built from glad's bundled Khronos spec
# instead of whatever is on the registry today - offline and deterministic.
# ---------------------------------------------------------------------------
if(TESSERA_BACKEND_OPENGL)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    FetchContent_Declare(glad_source
        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
        GIT_TAG        v0.1.36
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  cmake-is-not-used-here)  # populate only, never add_subdirectory
    FetchContent_MakeAvailable(glad_source)

    set(TESSERA_GLAD_DIR ${CMAKE_BINARY_DIR}/glad)
    add_custom_command(
        OUTPUT  ${TESSERA_GLAD_DIR}/src/glad.c ${TESSERA_GLAD_DIR}/include/glad/glad.h
        COMMAND ${Python3_EXECUTABLE} -m glad --profile core --api "gl=3.3"
                --generator c --spec gl --reproducible --out-path ${TESSERA_GLAD_DIR}
        WORKING_DIRECTORY ${glad_source_SOURCE_DIR}
        COMMENT "Generating OpenGL 3.3 core loader with glad"
        VERBATIM)

    add_library(glad STATIC ${TESSERA_GLAD_DIR}/src/glad.c)
    target_include_directories(glad PUBLIC ${TESSERA_GLAD_DIR}/include)
    target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})
    if(WIN32)
        # glad resolves entry points through wglGetProcAddress, but the 1.1
        # symbols still come from the import library.
        target_link_libraries(glad PUBLIC opengl32)
    endif()
endif()

# ---------------------------------------------------------------------------
# stb - image load / write (single header, wrapped as an interface target)
# ---------------------------------------------------------------------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        f0569113c93ad095470c54bf34a17b36646bbbb5)
FetchContent_MakeAvailable(stb)
add_library(stb INTERFACE)
# SYSTEM: stb is third-party, and its warnings are not ours to fix.
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Dear ImGui - no upstream CMakeLists, so compile the core + backends here
# ---------------------------------------------------------------------------
if(TESSERA_WITH_UI)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
    target_include_directories(imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC glfw)

    # The ImGui render backend has to match the render backend we present with.
    if(TESSERA_BACKEND_OPENGL)
        target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
        # PRIVATE: consumers link glad themselves, and linking it twice makes the
        # linker warn about duplicate archives.
        target_link_libraries(imgui PRIVATE glad)
        target_include_directories(imgui SYSTEM PUBLIC ${TESSERA_GLAD_DIR}/include)
        target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)
    endif()

    if(MSVC)
        target_compile_options(imgui PRIVATE /utf-8)
    endif()
endif()

# ---------------------------------------------------------------------------
# Assimp - broad format coverage (import + export)
# ---------------------------------------------------------------------------
if(TESSERA_WITH_ASSIMP)
    if(NOT TESSERA_PREFER_BUNDLED_DEPS)
        find_package(assimp CONFIG QUIET)
    endif()
    if(NOT assimp_FOUND)
        message(STATUS "assimp not found, fetching source (this build takes a few minutes)")
        set(ASSIMP_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_SAMPLES      OFF CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
        set(ASSIMP_INSTALL            OFF CACHE BOOL "" FORCE)
        set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS         OFF CACHE BOOL "" FORCE)

        # Prefer the system zlib. Assimp vendors a copy old enough to test
        # `defined(TARGET_OS_MAC)` as a classic Mac OS marker; modern SDKs
        # always define it, so that copy does `#define fdopen(fd,mode) NULL`
        # and breaks <stdio.h> for every file after it. Windows has no system
        # zlib, so the bundled one stays in play there.
        find_package(ZLIB QUIET)
        if(ZLIB_FOUND)
            set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
        else()
            set(ASSIMP_BUILD_ZLIB ON  CACHE BOOL "" FORCE)
        endif()
        FetchContent_Declare(assimp
            GIT_REPOSITORY https://github.com/assimp/assimp.git
            GIT_TAG        v5.4.3
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(assimp)
    endif()
endif()
