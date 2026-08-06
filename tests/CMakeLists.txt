# Smoke tests.
#
# Deliberately split by label so a machine without a graphics context can still
# run most of them:
#
#   cli      - argument handling and the registries; no file or device access
#   convert  - the whole import/export path; no graphics context needed, because
#              tessera_core links without one
#   render   - needs a working OpenGL 3.3 context
#
#   ctest -LE render     everything that works headless anywhere
#   ctest -L render      just the graphics tests

set(MODELS ${CMAKE_CURRENT_SOURCE_DIR}/models)
set(SCRATCH ${CMAKE_CURRENT_BINARY_DIR}/output)
file(MAKE_DIRECTORY ${SCRATCH})

# ---------------------------------------------------------------------------
# Command line surface
# ---------------------------------------------------------------------------
function(tessera_cli_test name expected)
    add_test(NAME cli.${name} COMMAND tessera ${ARGN})
    set_tests_properties(cli.${name} PROPERTIES
        LABELS cli
        PASS_REGULAR_EXPRESSION "${expected}")
endfunction()

tessera_cli_test(version    "^tessera [0-9]+\\.[0-9]+\\.[0-9]+" --version)
tessera_cli_test(help       "Usage"                             --help)
tessera_cli_test(formats    "readable formats"                  --formats)
tessera_cli_test(exporters  "writable formats"                  --export-formats)
tessera_cli_test(backends   "opengl"                            --list-backends)

# The native readers must win over the general-purpose backend, which is the
# whole point of the importer priority ordering.
add_test(NAME cli.native_readers_win COMMAND tessera --formats)
set_tests_properties(cli.native_readers_win PROPERTIES
    LABELS cli
    PASS_REGULAR_EXPRESSION "Wavefront OBJ")

# A file type nobody supports should fail loudly rather than silently.
add_test(NAME cli.rejects_unknown_format
         COMMAND tessera ${MODELS}/cube.obj -o ${SCRATCH}/nope.qqq)
set_tests_properties(cli.rejects_unknown_format PROPERTIES
    LABELS cli
    WILL_FAIL TRUE)

# ---------------------------------------------------------------------------
# Import and export, exercising each native reader plus the Assimp path
# ---------------------------------------------------------------------------
function(tessera_convert_test name input output)
    add_test(NAME convert.${name}
        COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:tessera>
            -DINPUT=${input}
            -DOUTPUT=${output}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunConvert.cmake)
    set_tests_properties(convert.${name} PROPERTIES LABELS convert)
endfunction()

tessera_convert_test(obj_to_stl ${MODELS}/cube.obj  ${SCRATCH}/cube.stl)
tessera_convert_test(obj_to_ply ${MODELS}/cube.obj  ${SCRATCH}/cube.ply)
tessera_convert_test(stl_to_obj ${MODELS}/tetra.stl ${SCRATCH}/tetra.obj)
tessera_convert_test(ply_to_stl ${MODELS}/quad.ply  ${SCRATCH}/quad.stl)

if(TESSERA_WITH_ASSIMP)
    # Round trip through a format only the general-purpose backend can write.
    tessera_convert_test(obj_to_glb ${MODELS}/cube.obj ${SCRATCH}/cube.glb)
    tessera_convert_test(glb_to_obj ${SCRATCH}/cube.glb ${SCRATCH}/from_glb.obj)
    set_tests_properties(convert.glb_to_obj PROPERTIES DEPENDS convert.obj_to_glb)
endif()

# ---------------------------------------------------------------------------
# Headless rendering
# ---------------------------------------------------------------------------
function(tessera_render_test name input output)
    add_test(NAME render.${name}
        COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:tessera>
            -DINPUT=${input}
            -DOUTPUT=${output}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunRender.cmake)
    set_tests_properties(render.${name} PROPERTIES LABELS render)
endfunction()

tessera_render_test(cube ${MODELS}/cube.obj  ${SCRATCH}/cube.png)
tessera_render_test(ply  ${MODELS}/quad.ply  ${SCRATCH}/quad.png)
