# compare the output of transmission-show to a reference file.
# returns 0 if the files match, nonzero otherwise.
##

if(CMAKE_VERSION VERSION_LESS 3.14)
    # --ignore-eol was introduced in CMake 3.14
    message(STATUS "skipping transmission-show test; cmake version too old")
else()
    get_filename_component(torrent_basename "${torrent_file}" NAME)
    set(output_file ${CMAKE_CURRENT_BINARY_DIR}/${torrent_basename}.out)

    message(STATUS "transmission_show ${transmission_show}")
    message(STATUS "       input_file ${torrent_file}")
    message(STATUS "      output_file ${output_file}")
    message(STATUS "   reference_file ${reference_file}")

    # We want UTF-8
    set(ENV{LC_ALL} "en_US.UTF-8")

    # The app's output includes timestamps, so fake our TZ to ensure
    # the test doesn't depend on the physical TZ of the test machine
    set(ENV{TZ} "UTC")

    execute_process(
        COMMAND ${transmission_show} ${torrent_file}
        OUTPUT_FILE ${output_file}
        ERROR_FILE ${output_file})

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol ${reference_file} ${output_file}
        RESULT_VARIABLE STATUS)

    if(STATUS AND NOT STATUS EQUAL 0)
        file(READ ${reference_file} CONTENTS)
        message(STATUS "EXPECTED CONTENTS (${reference_file}):")
        message("${CONTENTS}")

        file(READ ${output_file} CONTENTS)
        message(STATUS "RECEIVED CONTENTS (${output_file}):")
        message("${CONTENTS}")

        find_program(DIFF_EXEC diff)
        if(NOT DIFF_EXEC)
            find_program(GIT_EXEC git)
            if(GIT_EXEC)
                set(DIFF_EXEC "${GIT_EXEC}" diff --no-index)
            endif()
        endif()
        if(DIFF_EXEC)
            message(STATUS "DIFF:")
            execute_process(COMMAND ${DIFF_EXEC} --unified ${reference_file} ${output_file})
        endif()

        file(REMOVE ${output_file})
        message(FATAL_ERROR "failed: files '${reference_file}' and '${output_file}' do not match")
    else()
        file(REMOVE ${output_file})
        message(STATUS "passed")
    endif()
endif()
