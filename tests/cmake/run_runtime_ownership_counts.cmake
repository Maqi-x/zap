set(ZIR_OUTPUT "${OUTPUT}.zir")

execute_process(
    COMMAND "${ZAPC}" "${INPUT}" -emit-zir -o "${ZIR_OUTPUT}"
    RESULT_VARIABLE zir_result
    OUTPUT_VARIABLE zir_stdout
    ERROR_VARIABLE zir_stderr
)

if(NOT zir_result EQUAL 0)
    message(FATAL_ERROR "instrumented ownership-count ZIR emission failed:\n${zir_stdout}${zir_stderr}")
endif()

file(READ "${ZIR_OUTPUT}" emitted_zir)
string(FIND "${emitted_zir}" "ownership.destroy." edge_cleanup_offset)
if(edge_cleanup_offset EQUAL -1)
    message(FATAL_ERROR "instrumented ownership-count ZIR has no edge cleanup block")
endif()

execute_process(
    COMMAND "${ZAPC}" "${INPUT}" -o "${OUTPUT}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)

if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "instrumented ownership-count compilation failed:\n${compile_stdout}${compile_stderr}")
endif()

execute_process(
    COMMAND "${OUTPUT}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)

if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "instrumented ownership-count program failed with ${run_result}:\n${run_stdout}${run_stderr}")
endif()
