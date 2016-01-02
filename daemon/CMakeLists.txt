project(trdaemon)

if(WITH_SYSTEMD)
    add_definitions(-DUSE_SYSTEMD_DAEMON)
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

set_target_properties(${TR_NAME}-daemon PROPERTIES
    COMPILE_FLAGS "${SYSTEMD_DAEMON_CFLAGS}"
    LINK_FLAGS "${SYSTEMD_DAEMON_LDFLAGS}"
)

target_link_libraries(${TR_NAME}-daemon
    ${TR_NAME}
    ${EVENT2_LIBRARIES}
    ${SYSTEMD_DAEMON_LIBRARIES}
)

tr_win32_app_info(${PROJECT_NAME}_remote_WIN32_RC_FILE
    "Transmission Utility ('remote')"
    "${TR_NAME}-remote"
    "${TR_NAME}-remote.exe")

add_executable(${TR_NAME}-remote remote.c ${${PROJECT_NAME}_remote_WIN32_RC_FILE})

target_link_libraries(${TR_NAME}-remote
    ${TR_NAME}
    ${CURL_LIBRARIES}
)

foreach(P daemon remote)
    install(TARGETS ${TR_NAME}-${P} DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(INSTALL_DOC)
        install(FILES ${TR_NAME}-${P}.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()
endforeach()
