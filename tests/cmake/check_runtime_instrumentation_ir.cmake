execute_process(
    COMMAND "${ZAPC}" "${INPUT}" -S -emit-llvm -o "${OUTPUT}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)

if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "instrumented IR compilation failed:\n${compile_stdout}${compile_stderr}")
endif()

file(READ "${OUTPUT}" emitted_ir)
foreach(symbol
        zap_runtime_ownership_note_copy
        zap_runtime_ownership_note_drop
        zap_runtime_ownership_note_strong_retain
        zap_runtime_ownership_note_strong_release
        zap_runtime_ownership_note_destroy
        zap_arc_default_context
        zap_arc_deallocate
        zap_arc_collect_at_safepoint)
    string(FIND "${emitted_ir}" "${symbol}" symbol_offset)
    if(symbol_offset EQUAL -1)
        message(FATAL_ERROR "instrumented IR does not call ${symbol}")
    endif()
endforeach()

string(FIND "${emitted_ir}" "zap_arc_cycle_collect" collect_offset)
if(NOT collect_offset EQUAL -1)
    message(FATAL_ERROR "instrumented IR calls the collector directly instead of scheduling it")
endif()

string(FIND "${emitted_ir}" "call ptr @zap_arc_default_context()" context_call_offset)
if(context_call_offset EQUAL -1)
    message(FATAL_ERROR "instrumented IR does not pass an ARC runtime context")
endif()
