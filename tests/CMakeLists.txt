include(TrGTest)

add_subdirectory(libtransmission)
if(ENABLE_UTILS)
    add_subdirectory(utils)
endif()
