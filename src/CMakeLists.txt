# ---------------------------------------------------------------------------
# tessera_core - format-agnostic scene representation + all file I/O.
# Has no window, no graphics API and no UI, so it links into headless tools
# and tests unchanged.
# ---------------------------------------------------------------------------
add_library(tessera_core STATIC
    core/Log.cpp
    scene/Scene.cpp
    io/FileUtil.cpp
    io/ImageLoader.cpp
    io/ImporterRegistry.cpp
    io/ObjImporter.cpp
    io/StlImporter.cpp
    io/PlyImporter.cpp
    io/Exporter.cpp
)

target_include_directories(tessera_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tessera_core PUBLIC ${TESSERA_GLM_TARGET} PRIVATE stb)

if(TESSERA_WITH_ASSIMP)
    target_sources(tessera_core PRIVATE io/AssimpImporter.cpp)
    target_compile_definitions(tessera_core PUBLIC TESSERA_WITH_ASSIMP)
    target_link_libraries(tessera_core PRIVATE assimp::assimp)
endif()

tessera_configure_target(tessera_core)

# ---------------------------------------------------------------------------
# tessera - the viewer application.
# ---------------------------------------------------------------------------
add_executable(tessera
    main.cpp
    app/Application.cpp
    app/CommandLine.cpp
    camera/Camera.cpp
    gfx/RenderBackend.cpp
    tools/Picker.cpp
    ui/UiLayer.cpp
)

target_link_libraries(tessera PRIVATE tessera_core glfw stb)
target_compile_definitions(tessera PRIVATE TESSERA_VERSION="${PROJECT_VERSION}")

# Desktop integration differs enough per platform to be worth separate files
# rather than one file full of #ifdefs.
if(APPLE)
    target_sources(tessera PRIVATE app/PlatformIntegration_mac.mm)
    target_link_libraries(tessera PRIVATE "-framework AppKit" "-framework Foundation")
else()
    target_sources(tessera PRIVATE app/PlatformIntegration_generic.cpp)
endif()

# ---------------------------------------------------------------------------
# Render backends. Each one contributes its own sources, definitions and link
# libraries; nothing else in the project changes when one is switched on or off.
# ---------------------------------------------------------------------------
if(TESSERA_BACKEND_OPENGL)
    target_sources(tessera PRIVATE
        gfx/opengl/OpenGLBackend.cpp
        gfx/opengl/Shader.cpp
        gfx/opengl/GpuMesh.cpp
        gfx/opengl/GpuTexture.cpp
        gfx/opengl/Framebuffer.cpp)
    target_compile_definitions(tessera PRIVATE TESSERA_BACKEND_OPENGL)
    target_link_libraries(tessera PRIVATE glad OpenGL::GL)
endif()

if(TESSERA_BACKEND_VULKAN)
    target_sources(tessera PRIVATE gfx/vulkan/VulkanBackend.cpp)
    target_compile_definitions(tessera PRIVATE TESSERA_BACKEND_VULKAN)
    target_link_libraries(tessera PRIVATE Vulkan::Vulkan)
endif()

if(TESSERA_BACKEND_METAL)
    target_sources(tessera PRIVATE gfx/metal/MetalBackend.mm)
    target_compile_definitions(tessera PRIVATE TESSERA_BACKEND_METAL)
    target_link_libraries(tessera PRIVATE
        ${TESSERA_METAL_FRAMEWORK}
        ${TESSERA_QUARTZCORE_FRAMEWORK}
        ${TESSERA_FOUNDATION_FRAMEWORK})
    # ARC keeps the Objective-C object lifetimes correct without manual retains.
    set_source_files_properties(gfx/metal/MetalBackend.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
endif()

if(TESSERA_BACKEND_CUDA)
    target_sources(tessera PRIVATE gfx/cuda/CudaBackend.cpp)
    target_compile_definitions(tessera PRIVATE TESSERA_BACKEND_CUDA)
    target_link_libraries(tessera PRIVATE CUDA::cudart)
endif()

if(TESSERA_BACKEND_OPTIX)
    target_sources(tessera PRIVATE gfx/optix/OptixBackend.cpp)
    target_compile_definitions(tessera PRIVATE TESSERA_BACKEND_OPTIX)
    target_include_directories(tessera PRIVATE ${TESSERA_OPTIX_INCLUDE_DIR})
    # OptiX resolves its entry points through the driver at runtime, so only the
    # CUDA runtime and the dynamic loader are linked.
    target_link_libraries(tessera PRIVATE CUDA::cudart ${CMAKE_DL_LIBS})
endif()

# UiLayer.cpp compiles either way: with the UI off it provides no-op stubs, so
# the application code needs no conditionals of its own.
if(TESSERA_WITH_UI)
    target_compile_definitions(tessera PRIVATE TESSERA_WITH_UI)
    target_link_libraries(tessera PRIVATE imgui)
endif()

tessera_configure_target(tessera)
tessera_configure_bundle(tessera)
