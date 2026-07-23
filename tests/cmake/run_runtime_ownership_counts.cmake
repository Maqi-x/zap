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
