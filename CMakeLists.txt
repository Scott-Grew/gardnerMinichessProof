cmake_minimum_required(VERSION 3.20)
project(gardner_proof CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-O2 -Wall -Wextra -mbmi2)

add_library(gardner_core STATIC
  src/board.cpp
  src/attack_tables.cpp
  src/movegen.cpp
  src/canonical.cpp
  src/shared_table.cpp
  src/dfpn.cpp
  src/certificate.cpp
)
target_include_directories(gardner_core PUBLIC src)

add_executable(gardner-proof src/main.cpp)
target_link_libraries(gardner-proof PRIVATE gardner_core)

enable_testing()

add_executable(symmetry_test tests/symmetry_test.cpp)
target_link_libraries(symmetry_test PRIVATE gardner_core)

add_test(NAME symmetry COMMAND symmetry_test)
set_tests_properties(symmetry PROPERTIES
  LABELS fast
)

if(EXISTS "${CMAKE_SOURCE_DIR}/checker/perft_main.cpp")
  add_executable(checker_perft
    checker/mailbox_board.cpp
    checker/perft_main.cpp
  )
  target_include_directories(checker_perft PRIVATE checker)

  add_test(
    NAME perft_agreement
    COMMAND
      ${CMAKE_SOURCE_DIR}/tests/perft_agreement.sh
      $<TARGET_FILE:gardner-proof>
      $<TARGET_FILE:checker_perft>
  )
  set_tests_properties(perft_agreement PROPERTIES
    LABELS fast
  )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/checker/checker_main.cpp")
  add_executable(checker
    checker/mailbox_board.cpp
    checker/certificate_check.cpp
    checker/checker_main.cpp
  )
  target_include_directories(checker PRIVATE checker)

  add_test(
    NAME certificate_check
    COMMAND
      ${CMAKE_SOURCE_DIR}/tests/certificate_check.sh
      $<TARGET_FILE:gardner-proof>
      $<TARGET_FILE:checker>
  )
  set_tests_properties(certificate_check PROPERTIES
    LABELS fast
  )
endif()
