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
    ${EVENT2_INCLUDE_DIRS}
)

add_executable(${TR_NAME}-daemon
    daemon.c
    watch.c
    watch.h
)

set_target_properties(${TR_NAME}-daemon PROPERTIES
    COMPILE_FLAGS "${SYSTEMD_DAEMON_CFLAGS}"
    LINK_FLAGS "${SYSTEMD_DAEMON_LDFLAGS}"
)

target_link_libraries(${TR_NAME}-daemon
    ${TR_NAME}
    ${EVENT2_LIBRARIES}
)

add_executable(${TR_NAME}-remote remote.c)

target_include_directories(${TR_NAME}-remote PRIVATE
    ${CURL_INCLUDE_DIRS}
)

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
