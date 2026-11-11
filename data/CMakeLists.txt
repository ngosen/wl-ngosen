# The tray icons are the fcitx5 edition's, under names of this edition so both can be installed.
set(NGOSEN_CORE_ICONS "${PROJECT_SOURCE_DIR}/ngosen/data/icons/hicolor/scalable/apps")
install(FILES "${NGOSEN_CORE_ICONS}/fcitx-ngosen.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/status" RENAME wl-ngosen.svg)
install(FILES "${NGOSEN_CORE_ICONS}/fcitx-ngosen-off.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/status" RENAME wl-ngosen-off.svg)
