project(trgtk)

if(ENABLE_WERROR)
    add_compile_options($<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>)
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

find_program(APPSTREAM appstreamcli)

if(GTK_VERSION EQUAL 4)
    set(UI_SUBDIR ui/gtk4)
else()
    set(UI_SUBDIR ui/gtk3)
endif()

set(${PROJECT_NAME}_UI_FILES
    ${UI_SUBDIR}/AddTrackerDialog.ui
    ${UI_SUBDIR}/DetailsDialog.ui
    ${UI_SUBDIR}/EditTrackersDialog.ui
    ${UI_SUBDIR}/FilterBar.ui
    ${UI_SUBDIR}/MainWindow.ui
    ${UI_SUBDIR}/MakeDialog.ui
    ${UI_SUBDIR}/MakeProgressDialog.ui
    ${UI_SUBDIR}/MessageLogWindow.ui
    ${UI_SUBDIR}/OptionsDialog.ui
    ${UI_SUBDIR}/PrefsDialog.ui
    ${UI_SUBDIR}/RelocateDialog.ui
    ${UI_SUBDIR}/StatsDialog.ui
    ${UI_SUBDIR}/TorrentUrlChooserDialog.ui
)

macro(gtr_compile_resources NAME INPUT_DIR INPUT_FILE OUTPUT_FILE_BASE)
    add_custom_command(
        OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.c"
            "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.h"
        COMMAND
            "${GLIB_COMPILE_RESOURCES_EXECUTABLE}"
            "--target=${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.c"
            "--sourcedir=${INPUT_DIR}"
            --generate-source
            --c-name "${NAME}"
            "${INPUT_DIR}/${INPUT_FILE}"
        COMMAND
            "${GLIB_COMPILE_RESOURCES_EXECUTABLE}"
            "--target=${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.h"
            "--sourcedir=${INPUT_DIR}"
            --generate-header
            --c-name "${NAME}"
            "${INPUT_DIR}/${INPUT_FILE}"
        DEPENDS
            "${INPUT_DIR}/${INPUT_FILE}"
            ${ARGN}
        WORKING_DIRECTORY
            "${INPUT_DIR}"
    )
endmacro()

gtr_compile_resources(transmission ${CMAKE_CURRENT_SOURCE_DIR} transmission.gresource.xml transmission-resources
    icons/hicolor_apps_scalable_transmission.svg
    icons/lock.svg
    icons/options-symbolic.svg
    icons/ratio-symbolic.svg
    icons/turtle-symbolic.svg
    transmission-ui.css
    transmission-ui.xml
)
gtr_compile_resources(transmission_ui ${CMAKE_CURRENT_SOURCE_DIR}/${UI_SUBDIR} transmission-ui.gresource.xml transmission-ui-resources
    ${${PROJECT_NAME}_UI_FILES}
)

if(ENABLE_NLS)
    set(${PROJECT_NAME}_DESKTOP_FILE "${PROJECT_BINARY_DIR}/${TR_NAME}-gtk.desktop")
    add_custom_command(
        OUTPUT ${${PROJECT_NAME}_DESKTOP_FILE}
        COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --desktop -d ${CMAKE_SOURCE_DIR}/po --template ${PROJECT_SOURCE_DIR}/transmission-gtk.desktop.in -o ${${PROJECT_NAME}_DESKTOP_FILE}
        DEPENDS ${PROJECT_SOURCE_DIR}/transmission-gtk.desktop.in
        VERBATIM
    )

    set(${PROJECT_NAME}_METAINFO_FILE "${PROJECT_BINARY_DIR}/${TR_NAME}-gtk.metainfo.xml")
    add_custom_command(
        OUTPUT ${${PROJECT_NAME}_METAINFO_FILE}
        COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --xml -d ${CMAKE_SOURCE_DIR}/po --template ${PROJECT_SOURCE_DIR}/transmission-gtk.metainfo.xml.in -o ${${PROJECT_NAME}_METAINFO_FILE}
        DEPENDS ${PROJECT_SOURCE_DIR}/transmission-gtk.metainfo.xml.in
        VERBATIM
    )

    source_group("Generated Files" FILES ${${PROJECT_NAME}_DESKTOP_FILE} ${${PROJECT_NAME}_METAINFO_FILE})
endif()

set(${PROJECT_NAME}_SOURCES
    Actions.cc
    Application.cc
    DetailsDialog.cc
    Dialogs.cc
    FaviconCache.cc
    FileList.cc
    FilterBar.cc
    FreeSpaceLabel.cc
    IconCache.cc
    ListModelAdapter.cc
    main.cc
    MainWindow.cc
    MakeDialog.cc
    MessageLogWindow.cc
    Notify.cc
    OptionsDialog.cc
    PathButton.cc
    Prefs.cc
    PrefsDialog.cc
    RelocateDialog.cc
    Session.cc
    StatsDialog.cc
    SystemTrayIcon.cc
    Torrent.cc
    TorrentCellRenderer.cc
    TorrentFilter.cc
    TorrentSorter.cc
    Utils.cc
    ${CMAKE_CURRENT_BINARY_DIR}/transmission-resources.c
    ${CMAKE_CURRENT_BINARY_DIR}/transmission-ui-resources.c
)

set(${PROJECT_NAME}_HEADERS
    Actions.h
    Application.h
    DetailsDialog.h
    Dialogs.h
    FaviconCache.h
    FileList.h
    FilterBar.h
    Flags.h
    FreeSpaceLabel.h
    GtkCompat.h
    HigWorkarea.h
    IconCache.h
    ListModelAdapter.h
    MainWindow.h
    MakeDialog.h
    MessageLogWindow.h
    Notify.h
    OptionsDialog.h
    PathButton.h
    Prefs.h
    PrefsDialog.h
    RelocateDialog.h
    Session.h
    StatsDialog.h
    SystemTrayIcon.h
    Torrent.h
    TorrentCellRenderer.h
    TorrentFilter.h
    TorrentSorter.h
    Utils.h
    ${CMAKE_CURRENT_BINARY_DIR}/transmission-resources.h
    ${CMAKE_CURRENT_BINARY_DIR}/transmission-ui-resources.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}
)

include_directories(
    SYSTEM
    ${GTK${GTK_VERSION}_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${EVENT2_INCLUDE_DIRS}
)

if(WITH_APPINDICATOR)
    include_directories(SYSTEM ${APPINDICATOR_INCLUDE_DIRS})
endif()

link_directories(
    ${GTK${GTK_VERSION}_LIBRARY_DIRS}
)

add_definitions(
    "-DTRANSMISSIONLOCALEDIR=\"${CMAKE_INSTALL_FULL_LOCALEDIR}\""
    "-DGETTEXT_PACKAGE=\"${TR_NAME}-gtk\""
    -DG_DISABLE_DEPRECATED
    -DGDK_PIXBUF_DISABLE_DEPRECATED
    -DGDK_DISABLE_DEPRECATED
    -DGTK_DISABLE_DEPRECATED
    -DPANGO_DISABLE_DEPRECATED
    # FIXME: these break libnotify's headers
    # -DG_DISABLE_SINGLE_INCLUDES
    # -DGTK_DISABLE_SINGLE_INCLUDES
    -DGDKMM_DISABLE_DEPRECATED
    -DGIOMM_DISABLE_DEPRECATED
    -DGLIBMM_DISABLE_DEPRECATED
    -DGTKMM_DISABLE_DEPRECATED
    -DPANGOMM_DISABLE_DEPRECATED
    -DSIGCXX_DISABLE_DEPRECATED
    ${GTK${GTK_VERSION}_CFLAGS_OTHER}
)

if(ENABLE_UTP)
    add_definitions(-DWITH_UTP)
endif()

if(WITH_APPINDICATOR)
    set_property(SOURCE SystemTrayIcon.cc APPEND PROPERTY COMPILE_DEFINITIONS HAVE_APPINDICATOR)
    if(APPINDICATOR_IS_AYATANA)
        set_property(SOURCE SystemTrayIcon.cc APPEND PROPERTY COMPILE_DEFINITIONS APPINDICATOR_IS_AYATANA)
    endif()
endif()

tr_win32_app_info(${PROJECT_NAME}_WIN32_RC_FILE
    "Transmission GTK+ Client"
    "${TR_NAME}-gtk"
    "${TR_NAME}-gtk.exe"
    "${TR_NAME}.ico")

add_executable(${TR_NAME}-gtk WIN32
    ${${PROJECT_NAME}_SOURCES}
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_DESKTOP_FILE}
    ${${PROJECT_NAME}_METAINFO_FILE}
    ${${PROJECT_NAME}_WIN32_RC_FILE}
)

target_link_libraries(${TR_NAME}-gtk
    ${TR_NAME}
    ${GTK${GTK_VERSION}_LIBRARIES}
    ${CURL_LIBRARIES}
    ${EVENT2_LIBRARIES}
)

if(WITH_APPINDICATOR)
    target_link_libraries(${TR_NAME}-gtk ${APPINDICATOR_LIBRARIES})
endif()

if(NOT MSVC)
    target_compile_options(${TR_NAME}-gtk PRIVATE -Wno-exit-time-destructors)
endif()

if(MSVC)
    tr_append_target_property(${TR_NAME}-gtk LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

install(TARGETS ${TR_NAME}-gtk DESTINATION ${CMAKE_INSTALL_BINDIR})

set(${PROJECT_NAME}_PUBLIC_ICONS
    hicolor_apps_scalable_transmission.svg
    hicolor_apps_scalable_transmission-devel.svg
    hicolor_apps_symbolic_transmission-symbolic.svg
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
    install(FILES ${${PROJECT_NAME}_METAINFO_FILE} DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
else()
    install(FILES transmission-gtk.desktop.in DESTINATION ${CMAKE_INSTALL_DATADIR}/applications RENAME ${TR_NAME}-gtk.desktop)
endif()
