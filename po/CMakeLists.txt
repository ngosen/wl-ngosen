find_program(MSGFMT msgfmt REQUIRED)

set(NGOSEN_LANGUAGES vi)
set(NGOSEN_CATALOGS)
foreach(LANG ${NGOSEN_LANGUAGES})
    # Laid out as installed, so tests can point NGOSEN_WL_LOCALEDIR here.
    set(MO "${PROJECT_BINARY_DIR}/locale/${LANG}/LC_MESSAGES/wl-ngosen.mo")
    # --check-format catches a translation that drops or reorders a %s.
    add_custom_command(OUTPUT ${MO}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/locale/${LANG}/LC_MESSAGES"
        COMMAND ${MSGFMT} --check-format -o ${MO} ${CMAKE_CURRENT_SOURCE_DIR}/${LANG}.po
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${LANG}.po)
    list(APPEND NGOSEN_CATALOGS ${MO})
    install(FILES ${MO} DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${LANG}/LC_MESSAGES")
endforeach()
add_custom_target(translations ALL DEPENDS ${NGOSEN_CATALOGS})
