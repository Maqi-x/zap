set(LSP_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/lsp-main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/completion.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/configuration.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/position_codec.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/language_features.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/lsp.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/protocol_messages.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/protocol_utils.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/symbol_index.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/workspace.cpp"
  "${CMAKE_SOURCE_DIR}/src/lexer/lexer.cpp"
  "${CMAKE_SOURCE_DIR}/src/parser/parser.cpp"
  "${CMAKE_SOURCE_DIR}/src/frontend/module_loader.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder_calls.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder_conversions.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder_declaration_binding.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder_module_predeclare.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder_stmts.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder_exprs.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder_generic_functions.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder_generic_types.cpp"
  "${CMAKE_SOURCE_DIR}/src/sema/binder_types.cpp"
  "${CMAKE_SOURCE_DIR}/src/driver/args/argparse.cpp"
  "${CMAKE_SOURCE_DIR}/src/utils/stream.cpp"
)

add_executable(zap-lsp ${LSP_SOURCES})
target_link_libraries(zap-lsp PRIVATE zap_type_system)
target_link_libraries(zap-lsp PRIVATE zap_sema_conversions)
target_link_libraries(zap-lsp PRIVATE zap_type_layout)

target_include_directories(zap-lsp PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(zap-lsp PRIVATE -Wall -Wextra)
target_compile_definitions(zap-lsp PRIVATE ZAPC_CORE_DIR="${CMAKE_SOURCE_DIR}/core")
target_compile_definitions(zap-lsp PRIVATE ZAPC_STDLIB_DIR="${CMAKE_SOURCE_DIR}/std")

set_target_properties(zap-lsp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(BUILD_TESTING)
  add_executable(
    zap-lsp-configuration-tests
    ${CMAKE_SOURCE_DIR}/tests/cpp/lsp_configuration_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/configuration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lsp.cpp
  )
  target_include_directories(
    zap-lsp-configuration-tests PRIVATE "${CMAKE_SOURCE_DIR}/src"
  )
  target_compile_options(zap-lsp-configuration-tests PRIVATE -Wall -Wextra)
  add_test(NAME lsp-configuration COMMAND zap-lsp-configuration-tests)

  add_executable(
    zap-lsp-position-tests
    ${CMAKE_SOURCE_DIR}/tests/cpp/lsp_position_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/position_codec.cpp
  )
  target_include_directories(zap-lsp-position-tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
  target_compile_options(zap-lsp-position-tests PRIVATE -Wall -Wextra)
  add_test(NAME lsp-position COMMAND zap-lsp-position-tests)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME lsp-protocol
      COMMAND
        ${CMAKE_COMMAND} -E env
        ZAP_LSP_SERVER=$<TARGET_FILE:zap-lsp>
        ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lsp/run_lsp_tests.py
    )
  endif()
endif()
