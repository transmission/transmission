function(AddShowTest torrent_file reference_file)
   set(TRANSMISSION_SHOW_EXE ${CMAKE_BINARY_DIR}/utils/transmission-show${CMAKE_EXECUTABLE_SUFFIX})
   set(RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/run_transmission_show.cmake)

   add_test(transmission-show-ubuntu
      ${CMAKE_COMMAND}
      -D transmission_show:string=${TRANSMISSION_SHOW_EXE}
      -D torrent_file:string=${torrent_file}
      -D reference_file:string=${reference_file}
      -P ${RUNNER}
   )
endfunction()

function(AddAssetShowTest basename)
   set(ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
   AddShowTest(${ASSET_DIR}/${basename}.torrent ${ASSET_DIR}/${basename}.show)
endfunction()

AddAssetShowTest("ubuntu-20.04.3-desktop-amd64.iso")
