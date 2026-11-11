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

add_executable(toggle-key-test toggle-key.cpp)
target_link_libraries(toggle-key-test PRIVATE ngosen_wayland)
add_test(NAME toggle_key COMMAND toggle-key-test)
set_tests_properties(toggle_key PROPERTIES LABELS "unit")

add_executable(key-test key.cpp)
target_link_libraries(key-test PRIVATE ngosen_wayland)
add_test(NAME key COMMAND key-test)
set_tests_properties(key PROPERTIES LABELS "unit")

add_executable(tray-menu-test tray-menu.cpp)
target_link_libraries(tray-menu-test PRIVATE ngosen_wayland)
add_test(NAME tray_menu COMMAND tray-menu-test)
set_tests_properties(tray_menu PROPERTIES LABELS "unit")

# Run with --update to accept a change to the expected files after reading the diff.
add_executable(golden-test golden.cpp)
target_link_libraries(golden-test PRIVATE ngosen_shared)
add_test(NAME golden_files COMMAND golden-test ${CMAKE_CURRENT_SOURCE_DIR}/golden)
set_tests_properties(golden_files PROPERTIES LABELS "unit")

find_package(PkgConfig REQUIRED)
pkg_check_modules(TEST_XKB REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(TEST_GIO REQUIRED IMPORTED_TARGET gio-2.0)
add_library(test_compositor STATIC compositor/test-compositor.cpp compositor/test-bus.cpp)
target_include_directories(test_compositor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_compositor PUBLIC ngosen_protocols_server PkgConfig::TEST_XKB PkgConfig::TEST_GIO)

add_executable(wire-test wire.cpp)
target_link_libraries(wire-test PRIVATE test_compositor)
# The daemon looks for the dictionary where it is installed; the tests run from the build tree.
target_compile_definitions(wire-test PRIVATE TEST_LOCALEDIR="${PROJECT_BINARY_DIR}/locale")
add_dependencies(wire-test translations)
target_compile_definitions(wire-test PRIVATE TEST_DICTIONARY="${PROJECT_SOURCE_DIR}/ngosen/data/dictionaries/vietnamese.cm.dict")
if (NGOSEN_WL_SESSION_TESTS)
    add_test(NAME wire COMMAND wire-test $<TARGET_FILE:wl-ngosen>)
    set_tests_properties(wire PROPERTIES LABELS "wire" TIMEOUT 120)
endif()
