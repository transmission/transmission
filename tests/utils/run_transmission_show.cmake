# runs transmission-show on a file and
# compares transmission-show's output against a reference file.
# returns 0 if the files match, nonzero otherwise. 

get_filename_component(torrent_basename "${torrent_file}" NAME)
set(output_file ${CMAKE_CURRENT_BINARY_DIR}/${torrent_basename}.out)

message(STATUS "transmission_show ${transmission_show}")
message(STATUS "       input_file ${torrent_file}")
message(STATUS "      output_file ${output_file}")
message(STATUS "   reference_file ${reference_file}")

execute_process(
   COMMAND ${transmission_show} ${torrent_file}
   OUTPUT_FILE ${output_file}
)

execute_process(
   COMMAND ${CMAKE_COMMAND} -E compare_files ${reference_file} ${output_file}
   RESULT_VARIABLE STATUS
)

if(STATUS AND NOT STATUS EQUAL 0)
   message(FATAL_ERROR "failed: ${output_file} does not match ${expected}")
else()
   message(STATUS "passed")
endif()
