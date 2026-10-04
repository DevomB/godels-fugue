cmake_minimum_required(VERSION 3.16)
project(godels-fugue VERSION 1.7.0 LANGUAGES C)

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
  # A solver's state for the longest pieces takes about 280 KB, and
  # --sensitivity holds two on the stack under a deep search: give the main
  # thread the 8 MB stack it has on Linux and macOS and in the WebAssembly build.
  if(MSVC)
    add_link_options(/STACK:8388608)
  elseif(CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    add_link_options(LINKER:/STACK:8388608)
  else()
    add_link_options(LINKER:--stack,8388608)
  endif()
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
  src/midi_read.c
  src/model.c
  src/notation.c
  src/output.c
  src/page.c
  src/parts.c
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

add_executable(godels-fugue src/main.c)
target_link_libraries(godels-fugue PRIVATE canon)
target_compile_definitions(godels-fugue PRIVATE GODELS_FUGUE_VERSION="${PROJECT_VERSION}")

if(EMSCRIPTEN)
  # The web demo (web/) runs main() in a worker and reads the score files back
  # from the in-memory file system; tests/web_smoke.mjs does the same in Node.
  # The other tests need a native build.
  target_link_options(godels-fugue PRIVATE
    -sMODULARIZE=1 -sEXPORT_NAME=GodelsFugue -sENVIRONMENT=worker,node
    -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sFORCE_FILESYSTEM=1
    -sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB
    -sEXPORTED_RUNTIME_METHODS=callMain,FS)
  return()
endif()

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
    test_midi
    test_midi_read)
  add_executable(${name} tests/${name}.c)
  target_link_libraries(${name} PRIVATE canon)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endforeach()

add_test(NAME cli
  COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:godels-fugue> -DSRC=${CMAKE_SOURCE_DIR}
          -DOUT=${CMAKE_BINARY_DIR}/cli -P ${CMAKE_SOURCE_DIR}/tests/cli.cmake)
