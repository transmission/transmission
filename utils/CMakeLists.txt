foreach(P create edit remote show)
    add_executable(${TR_NAME}-${P})

    target_sources(${TR_NAME}-${P}
        PRIVATE
            ${P}.cc)

    target_include_directories(${TR_NAME}-${P}
        PRIVATE
            ${CMAKE_SOURCE_DIR})

    target_include_directories(${TR_NAME}-${P} SYSTEM
        PRIVATE
            ${LIBFMT_INCLUDE_DIRS}
            ${EVENT2_INCLUDE_DIRS}
            ${CURL_INCLUDE_DIRS})

    target_link_libraries(${TR_NAME}-${P}
        PRIVATE
            ${TR_NAME}
            ${CURL_LIBRARIES})

    tr_win32_app_info(${TR_NAME}-${P}
        "Transmission Utility ('${P}')"
        "${TR_NAME}-${P}"
        "${TR_NAME}-${P}.exe")

    install(
        TARGETS ${TR_NAME}-${P}
        DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(INSTALL_DOC)
        install(
            FILES ${TR_NAME}-${P}.1
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()
endforeach()
