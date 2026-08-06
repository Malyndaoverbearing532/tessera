# Runs a format conversion and checks that real output came out the other end.
#
# A CMake script rather than a shell script so the same test works on Windows,
# Linux and macOS without a second implementation in PowerShell.
#
# Expects: EXE, INPUT, OUTPUT

if(EXISTS "${OUTPUT}")
    file(REMOVE "${OUTPUT}")
endif()

execute_process(
    COMMAND "${EXE}" "${INPUT}" -o "${OUTPUT}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR
        "conversion failed with exit code ${exit_code}\n"
        "stdout:\n${stdout}\nstderr:\n${stderr}")
endif()

if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR
        "the command succeeded but wrote no file to ${OUTPUT}\n"
        "stdout:\n${stdout}\nstderr:\n${stderr}")
endif()

# A file that exists but holds only a header means the geometry was lost
# somewhere between the importer and the exporter.
file(SIZE "${OUTPUT}" written_bytes)
if(written_bytes LESS 120)
    message(FATAL_ERROR "output is only ${written_bytes} bytes, so the geometry is missing")
endif()

message(STATUS "wrote ${written_bytes} bytes to ${OUTPUT}")
