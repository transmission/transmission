project(trgtk)

if(WITH_LIBAPPINDICATOR)
    add_definitions(-DHAVE_LIBAPPINDICATOR)
endif()

execute_process(
    COMMAND
        ${PKG_CONFIG_EXECUTABLE} gio-2.0 --variable glib_compile_resources
    OUTPUT_VARIABLE
        GLIB_COMPILE_RESOURCES_EXECUTABLE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT GLIB_COMPILE_RESOURCES_EXECUTABLE)
    message(SEND_ERROR "Unable to find glib-compile-resources executable")
endif()

add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/transmission-resources.c
        ${CMAKE_CURRENT_BINARY_DIR}/transmission-resources.h
    COMMAND
        ${GLIB_COMPILE_RESOURCES_EXECUTABLE}
        --target=${CMAKE_CURRENT_BINARY_DIR}/transmission-resources.c
        --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}
        --generate-source
        --c-name transmission
        transmission.gresource.xml
    COMMAND
        ${GLIB_COMPILE_RESOURCES_EXECUTABLE}
        --target=${CMAKE_CURRENT_BINARY_DIR}/transmission-resources.h
        --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}
        --generate-header
        --c-name transmission
        transmission.gresource.xml
    DEPENDS
        icons/hicolor_apps_24x24_transmission.png
        icons/hicolor_apps_48x48_transmission.png
        icons/lock.png
        icons/ratio.png
        icons/turtle-blue.png
        icons/turtle-grey.png
        icons/utilities.png
        transmission.gresource.xml
        transmission-ui.xml
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(ENABLE_NLS)
    find_program(INTLTOOL_MERGE_EXECUTABLE intltool-merge REQUIRED)
    set(${PROJECT_NAME}_DESKTOP_FILE "${PROJECT_BINARY_DIR}/${TR_NAME}-gtk.desktop")
    add_custom_command(
        OUTPUT ${${PROJECT_NAME}_DESKTOP_FILE}
        COMMAND ${INTLTOOL_MERGE_EXECUTABLE} --desktop-style --utf8 ${CMAKE_SOURCE_DIR}/po ${PROJECT_SOURCE_DIR}/transmission-gtk.desktop.in ${${PROJECT_NAME}_DESKTOP_FILE}
        DEPENDS ${PROJECT_SOURCE_DIR}/transmission-gtk.desktop.in
        VERBATIM
    )
endif()

set(${PROJECT_NAME}_SOURCES
    actions.c
    conf.c
    details.c
    dialogs.c
    favicon.c
    file-list.c
    filter.c
    hig.c
    icons.c
    main.c
    makemeta-ui.c
    msgwin.c
    notify.c
    open-dialog.c
    relocate.c
    stats.c
    torrent-cell-renderer.c
    tr-core.c
    tr-icon.c
    tr-prefs.c
    tr-window.c
    util.c
    ${CMAKE_CURRENT_BINARY_DIR}/transmission-resources.c
)

set(${PROJECT_NAME}_HEADERS
    actions.h
    conf.h
    details.h
    dialogs.h
    favicon.h
    file-list.h
    filter.h
    hig.h
    icons.h
    makemeta-ui.h
    msgwin.h
    notify.h
    open-dialog.h
    relocate.h
    stats.h
    torrent-cell-renderer.h
    tr-core.h
    tr-icon.h
    tr-prefs.h
    tr-window.h
    util.h
    ${CMAKE_CURRENT_BINARY_DIR}/transmission-resources.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}
    ${LIBAPPINDICATOR_INCLUDE_DIRS}
    ${GTK_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${EVENT2_INCLUDE_DIRS}
)

link_directories(
    ${GTK_LIBRARY_DIRS}
)

add_definitions(
    "-DTRANSMISSIONLOCALEDIR=\"${CMAKE_INSTALL_FULL_LOCALEDIR}\""
    "-DGETTEXT_PACKAGE=\"${TR_NAME}-gtk\""
    -DG_DISABLE_DEPRECATED
    -DGDK_PIXBUF_DISABLE_DEPRECATED
    -DGDK_DISABLE_DEPRECATED
    # FIXME: migrate from GtkUIManager to GtkBuilder in 2.90
    # -DGTK_DISABLE_DEPRECATED
    -DPANGO_DISABLE_DEPRECATED
    # FIXME: these break libnotify's headers
    # -DG_DISABLE_SINGLE_INCLUDES
    # -DGTK_DISABLE_SINGLE_INCLUDES
    ${GTK_CFLAGS_OTHER}
)

tr_win32_app_info(${PROJECT_NAME}_WIN32_RC_FILE
    "Transmission GTK+ Client"
    "${TR_NAME}-gtk"
    "${TR_NAME}-gtk.exe"
    "${TR_NAME}.ico")

add_executable(${TR_NAME}-gtk WIN32
    ${${PROJECT_NAME}_SOURCES}
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_DESKTOP_FILE}
    ${${PROJECT_NAME}_WIN32_RC_FILE}
)

target_link_libraries(${TR_NAME}-gtk
    ${TR_NAME}
    ${LIBAPPINDICATOR_LIBRARIES}
    ${GTK_LIBRARIES}
    ${CURL_LIBRARIES}
    ${EVENT2_LIBRARIES}
)

if(MSVC)
    tr_append_target_property(${TR_NAME}-gtk LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

install(TARGETS ${TR_NAME}-gtk DESTINATION ${CMAKE_INSTALL_BINDIR})

set(${PROJECT_NAME}_PUBLIC_ICONS
    hicolor_apps_16x16_transmission.png
    hicolor_apps_22x22_transmission.png
    hicolor_apps_24x24_transmission.png
    hicolor_apps_32x32_transmission.png
    hicolor_apps_48x48_transmission.png
    hicolor_apps_256x256_transmission.png
    hicolor_apps_scalable_transmission.svg
)

set(ICON_NAME_REGEX "^([^_]+)_([^_]+)_([^_]+)_(.+)$")
foreach(ICON ${${PROJECT_NAME}_PUBLIC_ICONS})
    string(REGEX REPLACE ${ICON_NAME_REGEX} "\\1/\\3/\\2" ICON_DIR ${ICON})
    string(REGEX REPLACE ${ICON_NAME_REGEX} "\\4" ICON_NAME ${ICON})
    install(FILES icons/${ICON} DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/${ICON_DIR}/ RENAME ${ICON_NAME})
endforeach()

if(INSTALL_DOC)
    install(FILES ${TR_NAME}-gtk.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()

if(ENABLE_NLS)
    install(FILES ${${PROJECT_NAME}_DESKTOP_FILE} DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
else()
    install(FILES transmission-gtk.desktop.in DESTINATION ${CMAKE_INSTALL_DATADIR}/applications RENAME ${TR_NAME}-gtk.desktop)
endif()
