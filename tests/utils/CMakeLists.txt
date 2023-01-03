function(AddShowTest name file_basename)
    add_test(
        NAME ${name}
        COMMAND
            ${CMAKE_COMMAND}
            -D transmission_show:string=$<TARGET_FILE:transmission-show>
            -D torrent_file:string=assets/${file_basename}.torrent
            -D reference_file:string=${CMAKE_CURRENT_SOURCE_DIR}/assets/${file_basename}.show
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_transmission_show.cmake"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

# disabled AddShowTest(transmission-show-bittorrent-v2 bittorrent-v2-test)
addshowtest(transmission-show-bittorrent-v2-hybrid-test bittorrent-v2-hybrid-test)
addshowtest(transmission-show-inner-sanctum Inner_Sanctum_movie_archive)
addshowtest(transmission-show-thor Thor_and_the_Amazon_Women.avi)
addshowtest(transmission-show-ubuntu ubuntu-20.04.3-desktop-amd64.iso)
addshowtest(transmission-show-ubuntu-hybrid hybrid-single-ubuntu-20.04.3-desktop-amd64.iso)
