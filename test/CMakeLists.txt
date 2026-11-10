add_executable(config-test config.cpp)
target_link_libraries(config-test PRIVATE ngosen_shared)
add_test(NAME config COMMAND config-test)
set_tests_properties(config PROPERTIES LABELS "unit")

add_executable(app-modes-test app-modes.cpp)
target_link_libraries(app-modes-test PRIVATE ngosen_shared)
add_test(NAME app_modes COMMAND app-modes-test)
set_tests_properties(app_modes PROPERTIES LABELS "unit")

add_executable(program-name-test program-name.cpp)
target_link_libraries(program-name-test PRIVATE ngosen_shared)
add_test(NAME program_name COMMAND program-name-test)
set_tests_properties(program_name PROPERTIES LABELS "unit")

# Run with --update to accept a change to the expected files after reading the diff.
add_executable(golden-test golden.cpp)
target_link_libraries(golden-test PRIVATE ngosen_shared)
add_test(NAME golden_files COMMAND golden-test ${CMAKE_CURRENT_SOURCE_DIR}/golden)
set_tests_properties(golden_files PROPERTIES LABELS "unit")

find_package(PkgConfig REQUIRED)
pkg_check_modules(TEST_XKB REQUIRED IMPORTED_TARGET xkbcommon)
add_library(test_compositor STATIC compositor/test-compositor.cpp)
target_include_directories(test_compositor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_compositor PUBLIC ngosen_protocols_server PkgConfig::TEST_XKB)

add_executable(wire-test wire.cpp)
target_link_libraries(wire-test PRIVATE test_compositor)
add_test(NAME wire COMMAND wire-test $<TARGET_FILE:wl-ngosen>)
set_tests_properties(wire PROPERTIES LABELS "wire" TIMEOUT 120)
