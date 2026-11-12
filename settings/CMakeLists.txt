set(NGOSEN_SETTINGS_DIR "${NGOSEN_WL_DATADIR}/settings")
set(NGOSEN_LOCALEDIR "${CMAKE_INSTALL_FULL_LOCALEDIR}")
set(NGOSEN_DAEMON_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/wl-ngosen")
set(NGOSEN_SETTINGS_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/wl-ngosen-settings")
configure_file(wl-ngosen-settings.in ${CMAKE_CURRENT_BINARY_DIR}/wl-ngosen-settings @ONLY)
configure_file(io.github.ngosen.WlNgoSen.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.ngosen.WlNgoSen.desktop @ONLY)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/wl-ngosen-settings DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ngosen_settings DESTINATION ${NGOSEN_SETTINGS_DIR} FILES_MATCHING PATTERN "*.py")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.ngosen.WlNgoSen.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES io.github.ngosen.WlNgoSen.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

if (NGOSEN_WL_SESSION_TESTS)
    # The window tests need a display; without xvfb-run they are skipped.
    find_program(XVFB_RUN xvfb-run)
    set(display_runner)
    if (XVFB_RUN)
        set(display_runner ${XVFB_RUN} -a)
    endif()
    add_test(NAME settings_window COMMAND ${display_runner} ${Python3_EXECUTABLE} -m unittest discover -s tests -t . -v
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # GDK_BACKEND keeps GTK on the Xvfb display when the desktop session is Wayland.
    set_tests_properties(settings_window PROPERTIES ENVIRONMENT
        "NGOSEN_WL_DAEMON=$<TARGET_FILE:wl-ngosen>;NGOSEN_IBUS_DICTIONARY=${PROJECT_SOURCE_DIR}/ngosen/data/dictionaries/vietnamese.cm.dict;GDK_BACKEND=x11")
endif()

find_program(DESKTOP_FILE_VALIDATE desktop-file-validate)
if (DESKTOP_FILE_VALIDATE)
    add_test(NAME desktop_file COMMAND ${DESKTOP_FILE_VALIDATE} ${CMAKE_CURRENT_BINARY_DIR}/io.github.ngosen.WlNgoSen.desktop)
endif()
find_program(APPSTREAMCLI appstreamcli)
if (APPSTREAMCLI)
    add_test(NAME metainfo COMMAND ${APPSTREAMCLI} validate --no-net ${CMAKE_CURRENT_SOURCE_DIR}/io.github.ngosen.WlNgoSen.metainfo.xml)
endif()
