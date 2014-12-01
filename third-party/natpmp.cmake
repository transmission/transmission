cmake_minimum_required(VERSION 2.8)
project(natpmp C)

add_definitions(-DNATPMP_STATICLIB -DENABLE_STRNATPMPERR)

if(WIN32)
    set(${PROJECT_NAME}_ADD_SOURCES
        wingettimeofday.c
    )
endif()

add_library(${PROJECT_NAME} STATIC
    getgateway.c
    natpmp.c
    ${${PROJECT_NAME}_ADD_SOURCES}
)

install(TARGETS ${PROJECT_NAME} DESTINATION lib)
install(FILES declspec.h natpmp.h DESTINATION include)
