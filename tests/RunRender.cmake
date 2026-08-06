# Renders a model headlessly and checks the result is a genuine PNG.
#
# This is the only test that needs a working graphics context, which is why it
# carries the "render" label and is skipped where no rasteriser is available.
#
# Expects: EXE, INPUT, OUTPUT

if(EXISTS "${OUTPUT}")
    file(REMOVE "${OUTPUT}")
endif()

execute_process(
    COMMAND "${EXE}" "${INPUT}" -r "${OUTPUT}" -s 96x96
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR
        "render failed with exit code ${exit_code}\n"
        "stdout:\n${stdout}\nstderr:\n${stderr}")
endif()

if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "no image was written to ${OUTPUT}\nstderr:\n${stderr}")
endif()

# Verify the PNG signature rather than trusting the extension.
file(READ "${OUTPUT}" signature LIMIT 8 HEX)
if(NOT signature STREQUAL "89504e470d0a1a0a")
    message(FATAL_ERROR "output is not a PNG (leading bytes: ${signature})")
endif()

file(SIZE "${OUTPUT}" written_bytes)
if(written_bytes LESS 200)
    message(FATAL_ERROR "PNG is only ${written_bytes} bytes, so nothing was drawn")
endif()

message(STATUS "rendered ${written_bytes} bytes to ${OUTPUT}")
