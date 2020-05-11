project(fuzz)

include_directories(${CMAKE_SOURCE_DIR})

foreach(P fuzz-magnet fuzz-torrent)
    add_executable(${P} ${P}.cc ../libtransmission/magnet.c)
    target_compile_options(${P} PRIVATE -g -fsanitize=fuzzer,address -D__TRANSMISSION__)
    target_link_libraries(${P} PRIVATE -g -fsanitize=fuzzer,address ${TR_NAME})
endforeach()
