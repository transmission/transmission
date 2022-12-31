project(trgtk)

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
    ${UI_SUBDIR}/TorrentUrlChooserDialog.ui)

if(ENABLE_NLS)
    set(${PROJECT_NAME}_DESKTOP_FILE "${PROJECT_BINARY_DIR}/${TR_NAME}-gtk.desktop")
    add_custom_command(
        OUTPUT ${${PROJECT_NAME}_DESKTOP_FILE}
        COMMAND
            ${GETTEXT_MSGFMT_EXECUTABLE}
            --desktop
            -d ${CMAKE_SOURCE_DIR}/po
            --template ${PROJECT_SOURCE_DIR}/transmission-gtk.desktop.in
            -o ${${PROJECT_NAME}_DESKTOP_FILE}
        DEPENDS ${PROJECT_SOURCE_DIR}/transmission-gtk.desktop.in
        VERBATIM)

    set(${PROJECT_NAME}_METAINFO_FILE "${PROJECT_BINARY_DIR}/${TR_NAME}-gtk.metainfo.xml")
    add_custom_command(
        OUTPUT ${${PROJECT_NAME}_METAINFO_FILE}
        COMMAND
            ${GETTEXT_MSGFMT_EXECUTABLE}
            --xml
            -d ${CMAKE_SOURCE_DIR}/po
            --template ${PROJECT_SOURCE_DIR}/transmission-gtk.metainfo.xml.in
            -o ${${PROJECT_NAME}_METAINFO_FILE}
        DEPENDS ${PROJECT_SOURCE_DIR}/transmission-gtk.metainfo.xml.in
        VERBATIM)

    source_group("Generated Files"
        FILES
            ${${PROJECT_NAME}_DESKTOP_FILE}
            ${${PROJECT_NAME}_METAINFO_FILE})
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
    Utils.cc)

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
    Utils.h)

link_directories(
    ${GTK${GTK_VERSION}_LIBRARY_DIRS})

if(WITH_APPINDICATOR)
    set_property(
        SOURCE SystemTrayIcon.cc
        APPEND
        PROPERTY COMPILE_DEFINITIONS
            HAVE_APPINDICATOR)
    if(APPINDICATOR_IS_AYATANA)
        set_property(
            SOURCE SystemTrayIcon.cc
            APPEND
            PROPERTY COMPILE_DEFINITIONS
                APPINDICATOR_IS_AYATANA)
    endif()
endif()

add_executable(${TR_NAME}-gtk WIN32)

target_sources(${TR_NAME}-gtk
    PRIVATE
        ${${PROJECT_NAME}_SOURCES}
        ${${PROJECT_NAME}_HEADERS}
        ${${PROJECT_NAME}_DESKTOP_FILE}
        ${${PROJECT_NAME}_METAINFO_FILE})

tr_target_glib_resources(${TR_NAME}-gtk
    transmission.gresource.xml
    ${UI_SUBDIR}/transmission-ui.gresource.xml)

target_compile_definitions(${TR_NAME}-gtk
    PRIVATE
        TRANSMISSIONLOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
        GETTEXT_PACKAGE="${TR_NAME}-gtk"
        G_DISABLE_DEPRECATED
        GDK_PIXBUF_DISABLE_DEPRECATED
        GDK_DISABLE_DEPRECATED
        GTK_DISABLE_DEPRECATED
        PANGO_DISABLE_DEPRECATED
        # FIXME: these break libnotify's headers
        # G_DISABLE_SINGLE_INCLUDES
        # GTK_DISABLE_SINGLE_INCLUDES
        GDKMM_DISABLE_DEPRECATED
        GIOMM_DISABLE_DEPRECATED
        GLIBMM_DISABLE_DEPRECATED
        GTKMM_DISABLE_DEPRECATED
        PANGOMM_DISABLE_DEPRECATED
        SIGCXX_DISABLE_DEPRECATED
        $<$<BOOL:${ENABLE_UTP}>:WITH_UTP>)

target_compile_options(${TR_NAME}-gtk
    PRIVATE
        ${GTK${GTK_VERSION}_CFLAGS_OTHER}
        $<$<BOOL:${ENABLE_WERROR}>:$<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>>)

target_include_directories(${TR_NAME}-gtk
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${PROJECT_BINARY_DIR})

target_include_directories(${TR_NAME}-gtk SYSTEM
    PRIVATE
        ${LIBFMT_INCLUDE_DIRS}
        ${GTK${GTK_VERSION}_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
        ${EVENT2_INCLUDE_DIRS}
        $<$<BOOL:${WITH_APPINDICATOR}>:${APPINDICATOR_INCLUDE_DIRS}>)

target_link_libraries(${TR_NAME}-gtk
    ${TR_NAME}
    ${GTK${GTK_VERSION}_LIBRARIES}
    ${CURL_LIBRARIES}
    ${EVENT2_LIBRARIES})

if(WITH_APPINDICATOR)
    target_link_libraries(${TR_NAME}-gtk
        ${APPINDICATOR_LIBRARIES})
endif()

if(NOT MSVC)
    target_compile_options(${TR_NAME}-gtk
        PRIVATE
            -Wno-exit-time-destructors)
endif()

if(MSVC)
    tr_append_target_property(${TR_NAME}-gtk LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

tr_win32_app_info(${TR_NAME}-gtk
    "Transmission GTK+ Client"
    "${TR_NAME}-gtk"
    "${TR_NAME}-gtk.exe"
    "${TR_NAME}.ico")

install(
    TARGETS ${TR_NAME}-gtk
    DESTINATION ${CMAKE_INSTALL_BINDIR})

set(${PROJECT_NAME}_PUBLIC_ICONS
    hicolor_apps_scalable_transmission.svg
    hicolor_apps_scalable_transmission-devel.svg
    hicolor_apps_symbolic_transmission-symbolic.svg)

set(ICON_NAME_REGEX "^([^_]+)_([^_]+)_([^_]+)_(.+)$")
foreach(ICON ${${PROJECT_NAME}_PUBLIC_ICONS})
    string(REGEX REPLACE ${ICON_NAME_REGEX} "\\1/\\3/\\2" ICON_DIR ${ICON})
    string(REGEX REPLACE ${ICON_NAME_REGEX} "\\4" ICON_NAME ${ICON})
    install(
        FILES icons/${ICON}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/${ICON_DIR}/
        RENAME ${ICON_NAME})
endforeach()

if(INSTALL_DOC)
    install(
        FILES ${TR_NAME}-gtk.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()

if(ENABLE_NLS)
    install(
        FILES ${${PROJECT_NAME}_DESKTOP_FILE}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(
        FILES ${${PROJECT_NAME}_METAINFO_FILE}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
else()
    install(
        FILES transmission-gtk.desktop.in
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        RENAME ${TR_NAME}-gtk.desktop)
endif()
