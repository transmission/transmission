list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(TrGTest)

add_subdirectory(libtransmission)
if(ENABLE_UTILS)
    add_subdirectory(utils)
endif()
