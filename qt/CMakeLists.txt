project(trqt)

if(USE_QT5)
    macro(tr_qt_wrap_ui)
        qt5_wrap_ui(${ARGN})
    endmacro()
    macro(tr_qt_add_resources)
        qt5_add_resources(${ARGN})
    endmacro()
    macro(tr_qt_add_translation)
        qt5_add_translation(${ARGN})
    endmacro()
else()
    macro(tr_qt_wrap_ui)
        qt4_wrap_ui(${ARGN})
    endmacro()
    macro(tr_qt_add_resources)
        qt4_add_resources(${ARGN})
    endmacro()
    macro(tr_qt_add_translation)
        qt4_add_translation(${ARGN})
    endmacro()
endif()

set(${PROJECT_NAME}_SOURCES
    AboutDialog.cc
    AddData.cc
    Application.cc
    ColumnResizer.cc
    ComInteropHelper.cc
    DBusInteropHelper.cc
    DetailsDialog.cc
    FaviconCache.cc
    FileTreeDelegate.cc
    FileTreeItem.cc
    FileTreeModel.cc
    FileTreeView.cc
    FilterBar.cc
    FilterBarComboBox.cc
    FilterBarComboBoxDelegate.cc
    FilterBarLineEdit.cc
    Filters.cc
    Formatter.cc
    FreeSpaceLabel.cc
    IconToolButton.cc
    InteropHelper.cc
    InteropObject.cc
    LicenseDialog.cc
    MainWindow.cc
    MakeDialog.cc
    OptionsDialog.cc
    PathButton.cc
    Prefs.cc
    PrefsDialog.cc
    RelocateDialog.cc
    RpcClient.cc
    Session.cc
    SessionDialog.cc
    SqueezeLabel.cc
    StatsDialog.cc
    Torrent.cc
    TorrentDelegate.cc
    TorrentDelegateMin.cc
    TorrentFilter.cc
    TorrentModel.cc
    TorrentView.cc
    TrackerDelegate.cc
    TrackerModel.cc
    TrackerModelFilter.cc
    Utils.cc
    WatchDir.cc
)

if (NOT ENABLE_QT_COM_INTEROP)
    set_source_files_properties(ComInteropHelper.cc PROPERTIES HEADER_FILE_ONLY ON)
endif()
if (NOT ENABLE_QT_DBUS_INTEROP)
    set_source_files_properties(DBusInteropHelper.cc PROPERTIES HEADER_FILE_ONLY ON)
endif()

set(${PROJECT_NAME}_HEADERS
    AboutDialog.h
    AddData.h
    Application.h
    BaseDialog.h
    ColumnResizer.h
    ComInteropHelper.h
    CustomVariantType.h
    DBusInteropHelper.h
    DetailsDialog.h
    FaviconCache.h
    FileTreeDelegate.h
    FileTreeItem.h
    FileTreeModel.h
    FileTreeView.h
    FilterBar.h
    FilterBarComboBox.h
    FilterBarComboBoxDelegate.h
    FilterBarLineEdit.h
    Filters.h
    Formatter.h
    FreeSpaceLabel.h
    IconToolButton.h
    InteropHelper.h
    InteropObject.h
    LicenseDialog.h
    MainWindow.h
    MakeDialog.h
    OptionsDialog.h
    PathButton.h
    Prefs.h
    PrefsDialog.h
    RelocateDialog.h
    RpcClient.h
    Session.h
    SessionDialog.h
    Speed.h
    SqueezeLabel.h
    StatsDialog.h
    Torrent.h
    TorrentDelegate.h
    TorrentDelegateMin.h
    TorrentFilter.h
    TorrentModel.h
    TorrentView.h
    TrackerDelegate.h
    TrackerModel.h
    TrackerModelFilter.h
    Utils.h
    WatchDir.h
)

tr_qt_wrap_ui(${PROJECT_NAME}_UI_SOURCES
    AboutDialog.ui
    DetailsDialog.ui
    LicenseDialog.ui
    MainWindow.ui
    MakeDialog.ui
    MakeProgressDialog.ui
    OptionsDialog.ui
    PrefsDialog.ui
    RelocateDialog.ui
    SessionDialog.ui
    StatsDialog.ui
)

set(${PROJECT_NAME}_QRC_FILES application.qrc)
if(WIN32 OR APPLE)
    list(APPEND ${PROJECT_NAME}_QRC_FILES icons/Faenza/Faenza.qrc)
endif()

tr_qt_add_resources(${PROJECT_NAME}_QRC_SOURCES ${${PROJECT_NAME}_QRC_FILES})

set(${PROJECT_NAME}_LINGUAS
    de
    en
    es
    eu
    fr
    hu
    id
    it_IT
    kk
    ko
    lt
    pl_PL
    pt_BR
    ru
    uk
    zh_CN
)

if(ENABLE_NLS)
    set(${PROJECT_NAME}_ENABLED_LINGUAS ${${PROJECT_NAME}_LINGUAS})
else()
    set(${PROJECT_NAME}_ENABLED_LINGUAS)
endif()

set(${PROJECT_NAME}_TS_FILES)
foreach(LANG ${${PROJECT_NAME}_ENABLED_LINGUAS})
    list(APPEND ${PROJECT_NAME}_TS_FILES translations/transmission_${LANG}.ts)
endforeach()

if(${PROJECT_NAME}_TS_FILES)
    tr_qt_add_translation(${PROJECT_NAME}_QM_FILES ${${PROJECT_NAME}_TS_FILES})
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}
    ${CURL_INCLUDE_DIRS}
    ${EVENT2_INCLUDE_DIRS}
)

tr_win32_app_info(${PROJECT_NAME}_WIN32_RC_FILE
    "Transmission Qt Client"
    "transmission-qt"
    "transmission-qt.exe"
    "qtr.ico")

if(ENABLE_QT_COM_INTEROP)
    add_custom_command(
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/transmission-qt.tlb
        COMMAND
            ${MIDL_EXECUTABLE} /tlb ${CMAKE_CURRENT_BINARY_DIR}/transmission-qt.tlb transmission-qt.idl
        DEPENDS
            transmission-qt.idl
        WORKING_DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    list(APPEND ${PROJECT_NAME}_WIN32_RC_FILE transmission-qt.tlb.rc transmission-qt.idl ${CMAKE_CURRENT_BINARY_DIR}/transmission-qt.tlb)
    set_source_files_properties(transmission-qt.idl ${CMAKE_CURRENT_BINARY_DIR}/transmission-qt.tlb PROPERTIES HEADER_FILE_ONLY ON)
endif()

add_executable(${TR_NAME}-qt WIN32
    ${${PROJECT_NAME}_SOURCES}
    ${${PROJECT_NAME}_UI_SOURCES}
    ${${PROJECT_NAME}_QRC_SOURCES}
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_QM_FILES}
    ${${PROJECT_NAME}_WIN32_RC_FILE}
)

target_link_libraries(${TR_NAME}-qt
    ${TR_NAME}
    ${QT_TARGETS}
    ${CURL_LIBRARIES}
    ${EVENT2_LIBRARIES}
)

target_compile_definitions(${TR_NAME}-qt PRIVATE
    "TRANSLATIONS_DIR=\"${CMAKE_INSTALL_FULL_DATADIR}/${TR_NAME}/translations\""
    QT_NO_CAST_FROM_ASCII
    $<$<BOOL:${ENABLE_QT_COM_INTEROP}>:ENABLE_COM_INTEROP>
    $<$<BOOL:${ENABLE_QT_DBUS_INTEROP}>:ENABLE_DBUS_INTEROP>)

if(MSVC)
    tr_append_target_property(${TR_NAME}-qt LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

set_target_properties(${TR_NAME}-qt PROPERTIES AUTOMOC TRUE)

install(TARGETS ${TR_NAME}-qt DESTINATION ${CMAKE_INSTALL_BINDIR})

if(INSTALL_DOC)
    install(FILES ${TR_NAME}-qt.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()

install(FILES transmission-qt.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

if(ENABLE_NLS)
    install(FILES ${${PROJECT_NAME}_QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/${TR_NAME}/translations)
endif()
