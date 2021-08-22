cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

if(NOT CPACK_SOURCE_GENERATOR)
    return()
endif()

get_filename_component(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

file(GLOB_RECURSE SOURCE_FILES LIST_DIRECTORIES true RELATIVE "${SRC_DIR}" "${SRC_DIR}/*")
list(REVERSE SOURCE_FILES)

foreach(F IN LISTS SOURCE_FILES)
    if(NOT IS_SYMLINK "${SRC_DIR}/${F}")
        continue()
    endif()

    # Links to files seem to be staged correctly
    if(NOT IS_DIRECTORY "${SRC_DIR}/${F}")
        continue()
    endif()

    file(READ_SYMLINK "${SRC_DIR}/${F}" L)
    message(STATUS "Fixing link: ${F} -> ${L}")

    get_filename_component(D "${F}" DIRECTORY)
    file(MAKE_DIRECTORY "${CMAKE_INSTALL_PREFIX}/${D}/${L}")
    file(CREATE_LINK "${L}" "${CMAKE_INSTALL_PREFIX}/${F}" SYMBOLIC)
endforeach()
