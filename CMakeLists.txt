cmake_minimum_required(VERSION 3.16)
project(canon-collapse VERSION 0.2.0 LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
if(WIN32)
  # Clang on Windows uses the same C runtime headers as MSVC.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# The score page template is compiled into the binary.
set(SCORE_PAGE_C ${CMAKE_BINARY_DIR}/score_page_html.c)
add_custom_command(
  OUTPUT ${SCORE_PAGE_C}
  COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_SOURCE_DIR}/src/score_page.html
          -DOUT=${SCORE_PAGE_C} -DNAME=score_page_html
          -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/src/score_page.html ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  VERBATIM)

add_library(canon STATIC
  src/analyze.c
  src/canon.c
  src/check.c
  src/config.c
  src/corpus.c
  src/domain.c
  src/explain.c
  src/export.c
  src/json.c
  src/midi.c
  src/model.c
  src/notation.c
  src/output.c
  src/page.c
  src/proof.c
  src/run.c
  src/sat.c
  src/score.c
  src/solver.c
  src/theory.c
  src/trace.c
  ${SCORE_PAGE_C}
)
target_include_directories(canon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The math functions are part of the C runtime on Windows, whatever the compiler.
if(UNIX)
  target_link_libraries(canon PUBLIC m)
endif()

add_executable(canon-collapse src/main.c)
target_link_libraries(canon-collapse PRIVATE canon)

enable_testing()
foreach(name IN ITEMS
    test_domains
    test_theory
    test_json
    test_config
    test_canon
    test_check
    test_model
    test_constraints
    test_solver
    test_analyze
    test_properties
    test_proof
    test_export
    test_notation
    test_midi)
  add_executable(${name} tests/${name}.c)
  target_link_libraries(${name} PRIVATE canon)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endforeach()

add_test(NAME cli
  COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:canon-collapse> -DSRC=${CMAKE_SOURCE_DIR}
          -DOUT=${CMAKE_BINARY_DIR}/cli -P ${CMAKE_SOURCE_DIR}/tests/cli.cmake)
