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
    include(${QT_USE_FILE})

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
    about.cc
    add-data.cc
    app.cc
    dbus-adaptor.cc
    details.cc
    favicon.cc
    file-tree.cc
    filterbar.cc
    filters.cc
    formatter.cc
    freespace-label.cc
    hig.cc
    license.cc
    mainwin.cc
    make-dialog.cc
    options.cc
    prefs-dialog.cc
    prefs.cc
    relocate.cc
    session-dialog.cc
    session.cc
    squeezelabel.cc
    stats-dialog.cc
    torrent-delegate-min.cc
    torrent-delegate.cc
    torrent-filter.cc
    torrent-model.cc
    torrent.cc
    tracker-delegate.cc
    tracker-model-filter.cc
    tracker-model.cc
    tricontoolbutton.cc
    utils.cc
    watchdir.cc
)

set(${PROJECT_NAME}_HEADERS
    about.h
    add-data.h
    app.h
    dbus-adaptor.h
    details.h
    favicon.h
    file-tree.h
    filterbar.h
    filters.h
    formatter.h
    freespace-label.h
    hig.h
    license.h
    mainwin.h
    make-dialog.h
    options.h
    prefs-dialog.h
    prefs.h
    relocate.h
    session-dialog.h
    session.h
    speed.h
    squeezelabel.h
    stats-dialog.h
    torrent-delegate-min.h
    torrent-delegate.h
    torrent-filter.h
    torrent-model.h
    torrent.h
    tracker-delegate.h
    tracker-model-filter.h
    tracker-model.h
    tricontoolbutton.h
    types.h
    utils.h
    watchdir.h
)

tr_qt_wrap_ui(${PROJECT_NAME}_UI_SOURCES
    details.ui
    mainwin.ui
    session-dialog.ui
    stats-dialog.ui
)

tr_qt_add_resources(${PROJECT_NAME}_QRC_SOURCES
    application.qrc
)

set(${PROJECT_NAME}_LINGUAS
    en
    es
    eu
    fr
    hu
    kk
    lt
    pt_BR
    ru
    uk
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

add_definitions(
    "-DTRANSLATIONS_DIR=\"${CMAKE_INSTALL_FULL_DATADIR}/${TR_NAME}/translations\""
)

add_executable(${TR_NAME}-qt
    ${${PROJECT_NAME}_SOURCES}
    ${${PROJECT_NAME}_UI_SOURCES}
    ${${PROJECT_NAME}_QRC_SOURCES}
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_QM_FILES}
)

target_link_libraries(${TR_NAME}-qt
    ${TR_NAME}
    ${QT_LIBRARIES}
    ${CURL_LIBRARIES}
    ${EVENT2_LIBRARIES}
)

if(USE_QT5)
    qt5_use_modules(${TR_NAME}-qt ${TR_QT5_MODULES})
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
