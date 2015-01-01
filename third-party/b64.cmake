cmake_minimum_required(VERSION 2.8)
project(b64 C)

include_directories(include)

add_library(${PROJECT_NAME} STATIC
    src/cdecode.c
    src/cencode.c
)

install(TARGETS ${PROJECT_NAME} DESTINATION lib)
install(DIRECTORY include/b64 DESTINATION include)
