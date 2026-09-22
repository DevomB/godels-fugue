cmake_minimum_required(VERSION 3.16)
project(canon-collapse LANGUAGES C)

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

add_library(canon STATIC
  src/domain.c
  src/theory.c
  src/canon.c
  src/proof.c
  src/constraint.c
  src/solver.c
  src/midi.c
)
target_include_directories(canon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(NOT MSVC)
  target_link_libraries(canon PUBLIC m)
endif()

add_executable(canon-collapse src/main.c)
target_link_libraries(canon-collapse PRIVATE canon)

enable_testing()
foreach(name IN ITEMS
    test_domains
    test_theory
    test_canon
    test_constraints
    test_proof
    test_solver
    test_midi
    test_properties)
  add_executable(${name} tests/${name}.c)
  target_link_libraries(${name} PRIVATE canon)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()
