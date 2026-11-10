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
