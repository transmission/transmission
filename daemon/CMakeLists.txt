project(trdaemon)

if(WITH_INOTIFY)
    add_definitions(-DWITH_INOTIFY)
endif()

if(WITH_KQUEUE)
    add_definitions(-DWITH_KQUEUE)
endif()

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
    watch.c
)

if(WIN32)
    list(APPEND ${PROJECT_NAME}_SOURCES daemon-win32.c)
else()
    list(APPEND ${PROJECT_NAME}_SOURCES daemon-posix.c)
endif()

set(${PROJECT_NAME}_HEADERS
    daemon.h
    watch.h
)

add_executable(${TR_NAME}-daemon ${${PROJECT_NAME}_SOURCES} ${${PROJECT_NAME}_HEADERS})

set_target_properties(${TR_NAME}-daemon PROPERTIES
    COMPILE_FLAGS "${SYSTEMD_DAEMON_CFLAGS}"
    LINK_FLAGS "${SYSTEMD_DAEMON_LDFLAGS}"
)

target_link_libraries(${TR_NAME}-daemon
    ${TR_NAME}
    ${EVENT2_LIBRARIES}
)

add_executable(${TR_NAME}-remote remote.c)

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
