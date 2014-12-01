cmake_minimum_required(VERSION 2.8)
project(event C)

set(${PROJECT_NAME}_ADD_SOURCES
    win32select.c
    evthread_win32.c
    buffer_iocp.c
    event_iocp.c
    bufferevent_async.c
)

add_definitions(-DHAVE_CONFIG_H)

include_directories(include compat WIN32-Code)

add_library(${PROJECT_NAME} STATIC
    event.c
    buffer.c
    bufferevent.c
    bufferevent_sock.c
    bufferevent_pair.c
    listener.c
    evmap.c
    log.c
    evutil.c
    strlcpy.c
    signal.c
    bufferevent_filter.c
    evthread.c
    bufferevent_ratelim.c
    evutil_rand.c
    event_tagging.c
    http.c
    evdns.c
    evrpc.c
    ${${PROJECT_NAME}_ADD_SOURCES}
)

install(TARGETS ${PROJECT_NAME} DESTINATION lib)
install(DIRECTORY include/event2 DESTINATION include)
install(DIRECTORY WIN32-Code/event2 DESTINATION include)
