cmake_minimum_required(VERSION 2.8)
project(utp CXX)

if(WIN32)
    set(${PROJECT_NAME}_ADD_SOURCES
        win32_inet_ntop.cpp
    )
else()
    add_definitions(-DPOSIX)
endif()

include_directories(.)

add_library(${PROJECT_NAME} STATIC
    utp.cpp
    utp_utils.cpp
    ${${PROJECT_NAME}_ADD_SOURCES}
)

install(TARGETS ${PROJECT_NAME} DESTINATION lib)
install(FILES utp.h utypes.h DESTINATION include/libutp)
