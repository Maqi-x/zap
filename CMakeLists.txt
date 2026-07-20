cmake_minimum_required(VERSION 3.20)
project(zapc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Werror=switch)
endif()

option(ZAP_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ZAP_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "ZAP_ENABLE_SANITIZERS requires a GNU or Clang compiler")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

option(ZAP_ENABLE_RUNTIME_INSTRUMENTATION
       "Emit test-only runtime ownership instrumentation" OFF)

set(ZAP_STDLIB_COMPILE_DEFINITIONS)
if(ZAP_ENABLE_RUNTIME_INSTRUMENTATION)
    list(APPEND ZAP_STDLIB_COMPILE_DEFINITIONS -DZAP_RUNTIME_INSTRUMENTATION=1)
endif()

# Accept the system LLVM version and handle API differences in code when needed.
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

# Source files
set(SOURCES
    src/main.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/frontend/module_loader.cpp
    src/ir/ir_generator.cpp
    src/sema/binder.cpp
    src/sema/binder_calls.cpp
    src/sema/binder_conversions.cpp
    src/sema/binder_declaration_binding.cpp
    src/sema/binder_module_predeclare.cpp
    src/sema/binder_stmts.cpp
    src/sema/binder_exprs.cpp
    src/sema/binder_generic_functions.cpp
    src/sema/binder_generic_types.cpp
    src/sema/binder_types.cpp
    src/codegen/llvm_codegen.cpp
    src/codegen/llvm_codegen_constants.cpp
    src/codegen/llvm_codegen_zir.cpp
    src/codegen/llvm_codegen_arc.cpp
    src/codegen/class_arc_emitter.cpp
    src/driver/driver.cpp
    src/driver/process.cpp
    src/driver/args/argparse.cpp
    src/utils/stream.cpp
)

add_library(zap_type_system STATIC
    src/ir/failable_type.cpp
    src/ir/primitive_type.cpp
    src/ir/type_identity.cpp
)
target_include_directories(zap_type_system PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_options(zap_type_system PRIVATE -Wall -Wextra)

add_library(zap_sema_conversions STATIC src/sema/conversion.cpp)
target_include_directories(zap_sema_conversions PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_options(zap_sema_conversions PRIVATE -Wall -Wextra)
target_link_libraries(zap_sema_conversions PUBLIC zap_type_system)

add_library(zap_type_layout STATIC src/sema/type_layout.cpp)
target_include_directories(zap_type_layout PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_options(zap_type_layout PRIVATE -Wall -Wextra)
target_link_libraries(zap_type_layout PUBLIC zap_type_system)

add_library(zap_zir_verifier STATIC
    src/ir/ownership_flow.cpp
    src/ir/ownership_lowering.cpp
    src/ir/ownership_liveness.cpp
    src/ir/zir_verifier.cpp
    src/ir/zir_verifier_function.cpp
    src/ir/zir_verifier_types.cpp
)
target_include_directories(zap_zir_verifier PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_options(zap_zir_verifier PRIVATE -Wall -Wextra)
target_link_libraries(zap_zir_verifier PUBLIC zap_type_system)

add_executable(zapc ${SOURCES})

target_include_directories(zapc PRIVATE "${CMAKE_SOURCE_DIR}/src" ${LLVM_INCLUDE_DIRS})
target_compile_options(zapc PRIVATE -Wall -Wextra)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/stdlib.o
    COMMAND ${CMAKE_C_COMPILER} ${ZAP_STDLIB_COMPILE_DEFINITIONS} -c ${CMAKE_SOURCE_DIR}/src/stdlib.c -o ${CMAKE_BINARY_DIR}/stdlib.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/stdlib.c
            ${CMAKE_SOURCE_DIR}/src/runtime/arc_layout.h
            ${CMAKE_SOURCE_DIR}/src/runtime/string_layout.h
    COMMENT "Compiling stdlib.c"
)

add_custom_target(zap_stdlib ALL DEPENDS ${CMAKE_BINARY_DIR}/stdlib.o)
add_dependencies(zapc zap_stdlib)

# Needed LLVM components
set(LLVM_COMPONENTS
    core
    support
    all-targets
    mc
    target
    analysis
    passes
)

# Some distros (e.g. Arch Linux) provide llvm as one shared library rather than multiple ones.
if(LLVM_LINK_LLVM_DYLIB)
    # Single
    set(llvm_libs LLVM)
else()
    # Multiple
    llvm_map_components_to_libnames(llvm_libs ${LLVM_COMPONENTS})
endif()

target_link_libraries(zapc PRIVATE ${llvm_libs})
target_link_libraries(zapc PRIVATE zap_type_system)
target_link_libraries(zapc PRIVATE zap_sema_conversions)
target_link_libraries(zapc PRIVATE zap_type_layout)
target_link_libraries(zapc PRIVATE zap_zir_verifier)
target_compile_definitions(zapc PRIVATE ${LLVM_DEFINITIONS})
target_compile_definitions(zapc PRIVATE ZAPC_STDLIB_PATH="${CMAKE_BINARY_DIR}/stdlib.o")
target_compile_definitions(zapc PRIVATE ZAPC_CORE_DIR="${CMAKE_SOURCE_DIR}/core")
target_compile_definitions(zapc PRIVATE ZAPC_STDLIB_DIR="${CMAKE_SOURCE_DIR}/std")
if(ZAP_ENABLE_RUNTIME_INSTRUMENTATION)
    target_compile_definitions(zapc PRIVATE ZAP_RUNTIME_INSTRUMENTATION=1)
endif()

include(cmake/doxygen.cmake)

include(CTest)
if(BUILD_TESTING)
    add_executable(zap-type-identity-tests tests/cpp/type_identity_test.cpp)
    target_link_libraries(zap-type-identity-tests PRIVATE zap_type_system)
    target_compile_options(zap-type-identity-tests PRIVATE -Wall -Wextra)
    add_test(NAME type-identity COMMAND zap-type-identity-tests)

    add_executable(zap-conversion-tests tests/cpp/conversion_test.cpp)
    target_link_libraries(zap-conversion-tests PRIVATE zap_sema_conversions)
    target_compile_options(zap-conversion-tests PRIVATE -Wall -Wextra)
    add_test(NAME conversions COMMAND zap-conversion-tests)

    add_executable(zap-type-layout-tests tests/cpp/type_layout_test.cpp)
    target_link_libraries(zap-type-layout-tests PRIVATE zap_type_layout)
    target_compile_options(zap-type-layout-tests PRIVATE -Wall -Wextra)
    add_test(NAME type-layout COMMAND zap-type-layout-tests)

    add_executable(zap-zir-verifier-tests tests/cpp/zir_verifier_test.cpp)
    target_link_libraries(zap-zir-verifier-tests PRIVATE zap_zir_verifier)
    target_compile_options(zap-zir-verifier-tests PRIVATE -Wall -Wextra)
    add_test(NAME zir-verifier COMMAND zap-zir-verifier-tests)

    add_executable(zap-runtime-instrumentation-tests
        tests/cpp/runtime_instrumentation_test.c
        src/stdlib.c
    )
    target_include_directories(zap-runtime-instrumentation-tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(zap-runtime-instrumentation-tests PRIVATE ZAP_RUNTIME_INSTRUMENTATION=1)
    target_compile_options(zap-runtime-instrumentation-tests PRIVATE -Wall -Wextra)
    target_link_libraries(zap-runtime-instrumentation-tests PRIVATE m)
    add_test(NAME runtime-instrumentation COMMAND zap-runtime-instrumentation-tests)

    if(ZAP_ENABLE_RUNTIME_INSTRUMENTATION)
        add_test(
            NAME runtime-instrumentation-ir
            COMMAND ${CMAKE_COMMAND}
                -DZAPC=$<TARGET_FILE:zapc>
                -DINPUT=${CMAKE_SOURCE_DIR}/tests/class_arc_test.zp
                -DOUTPUT=${CMAKE_BINARY_DIR}/runtime-instrumentation.ll
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/check_runtime_instrumentation_ir.cmake
        )
    endif()
endif()

option(INCLUDE_LSP "Should the LSP binary be compiled" ON)
if(INCLUDE_LSP)
    add_subdirectory(src/lsp)
endif()
