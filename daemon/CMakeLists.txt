project(trdaemon)

if(WITH_SYSTEMD)
    add_definitions(-DUSE_SYSTEMD)
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${EVENT2_INCLUDE_DIRS}
)

set(${PROJECT_NAME}_SOURCES
    daemon.c
    daemon-posix.c
    daemon-win32.c
)

if(WIN32)
    set_source_files_properties(daemon-posix.c PROPERTIES HEADER_FILE_ONLY ON)
else()
    set_source_files_properties(daemon-win32.c PROPERTIES HEADER_FILE_ONLY ON)
endif()

set(${PROJECT_NAME}_HEADERS
    daemon.h
)

tr_win32_app_info(${PROJECT_NAME}_WIN32_RC_FILE
    "Transmission Daemon"
    "${TR_NAME}-daemon"
    "${TR_NAME}-daemon.exe")

add_executable(${TR_NAME}-daemon
    ${${PROJECT_NAME}_SOURCES}
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_WIN32_RC_FILE})

target_link_libraries(${TR_NAME}-daemon
    ${TR_NAME}
    ${EVENT2_LIBRARIES}
)

if(WITH_SYSTEMD)
    target_include_directories(${TR_NAME}-daemon PRIVATE ${SYSTEMD_INCLUDE_DIRS})
    target_link_libraries(${TR_NAME}-daemon ${SYSTEMD_LIBRARIES})
endif()

foreach(P daemon)
    install(TARGETS ${TR_NAME}-${P} DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(INSTALL_DOC)
        install(FILES ${TR_NAME}-${P}.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()
endforeach()
