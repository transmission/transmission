function(AddAssetShowTest name file_basename)
   add_test(
      NAME ${name}
      COMMAND ${CMAKE_COMMAND}
      -D transmission_show:string=$<TARGET_FILE:transmission-show>
      -D torrent_file:string=assets/${file_basename}.torrent
      -D reference_file:string=${CMAKE_CURRENT_SOURCE_DIR}/assets/${file_basename}.show
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_transmission_show.cmake"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
   )
endfunction()

AddAssetShowTest(transmission-show-ubuntu ubuntu-20.04.3-desktop-amd64.iso)
AddAssetShowTest(transmission-show-thor Thor_and_the_Amazon_Women.avi)
