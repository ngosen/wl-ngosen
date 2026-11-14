find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0>=2.72)
pkg_check_modules(XKB REQUIRED IMPORTED_TARGET xkbcommon)

# Copied from ibus-ngosen at the commit in COPIED_FROM; the names stay so the two can be compared.
add_library(ngosen_shared STATIC
    shared/ngosen-ibus-app-modes.cpp
    shared/ngosen-ibus-config.cpp
    shared/ngosen-ibus-i18n.cpp
    shared/ngosen-ibus-program-name.cpp
    shared/ngosen-ibus-resources.cpp
    shared/ngosen-ibus-typing-log.cpp
    ngosen-keys.cpp
)
target_include_directories(ngosen_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_definitions(ngosen_shared PUBLIC GETTEXT_PACKAGE="wl-ngosen" NGOSEN_IBUS_VERSION="${PROJECT_VERSION}")
target_compile_definitions(ngosen_shared PRIVATE NGOSEN_IBUS_DICTIONARY="${NGOSEN_WL_DATADIR}/vietnamese.cm.dict"
                                                  NGOSEN_IBUS_ENGLISH_WORDS="${NGOSEN_WL_DATADIR}/english-common.txt"
                                                  NGOSEN_IBUS_KEEP_WORDS="${NGOSEN_WL_DATADIR}/keep-words.txt")
# The core calls the shell's log (ngosen-log.h), so the log comes after the core on the link line.
add_library(ngosen_wl_log STATIC ngosen-log.cpp)
target_include_directories(ngosen_wl_log PRIVATE $<TARGET_PROPERTY:ngosen_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(ngosen_shared PUBLIC ngosen_core PkgConfig::GIO PkgConfig::XKB ngosen_wl_log)

add_library(ngosen_wayland STATIC
    ngosen-daemon.cpp
    ngosen-input-method.cpp
    ngosen-notice.cpp
    ngosen-toplevels.cpp
    ngosen-toggle-key.cpp
    ngosen-tray.cpp
    ngosen-tray-menu.cpp
    ngosen-wayland-host.cpp
    ngosen-wayland-key.cpp
)
target_include_directories(ngosen_wayland PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ngosen_wayland PUBLIC ngosen_shared ngosen_protocols_client)
# Bars look the icon up in the theme by name, or in this directory.
target_compile_definitions(ngosen_wayland PRIVATE NGOSEN_ICONDIR="${CMAKE_INSTALL_FULL_DATADIR}/icons"
    NGOSEN_SETTINGS_EXEC="${CMAKE_INSTALL_FULL_BINDIR}/wl-ngosen-settings")

add_executable(wl-ngosen main.cpp)
target_compile_definitions(wl-ngosen PRIVATE NGOSEN_LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_link_libraries(wl-ngosen PRIVATE ngosen_wayland)

install(TARGETS wl-ngosen DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/ngosen/data/dictionaries/vietnamese.cm.dict
              ${PROJECT_SOURCE_DIR}/ngosen/data/english-common.txt
              ${PROJECT_SOURCE_DIR}/ngosen/data/english-common.txt.license
              ${PROJECT_SOURCE_DIR}/ngosen/data/keep-words.txt
              ${PROJECT_SOURCE_DIR}/ngosen/data/keep-words.txt.license
        DESTINATION ${NGOSEN_WL_DATADIR})
