cmake_minimum_required(VERSION 3.16)

project(wl-ngosen VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(GNUInstallDirs)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ngosen/src/core/CMakeLists.txt")
    message(FATAL_ERROR "The typing logic is missing; run: git submodule update --init")
endif()

# The submodule's own option() predates CMP0077, so only a cache entry reaches it.
set(NGOSEN_CORE_ONLY ON CACHE BOOL "Build the typing logic without fcitx5" FORCE)
enable_testing()
add_subdirectory(ngosen)

set(NGOSEN_WL_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/wl-ngosen")
add_subdirectory(protocol)
add_subdirectory(src)
add_subdirectory(test)
