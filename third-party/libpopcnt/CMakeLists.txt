cmake_minimum_required(VERSION 2.8)
project(libpopcnt C)

add_library(${PROJECT_NAME} STATIC
    libpopcnt.h
)

install(TARGETS ${PROJECT_NAME} DESTINATION lib)
install(FILES libpopcnt.h DESTINATION include/libpopcnt)
