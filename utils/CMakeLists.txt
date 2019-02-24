project(trutils)

include_directories(${CMAKE_SOURCE_DIR})

foreach(P create edit remote show)
    tr_win32_app_info(${PROJECT_NAME}_${P}_WIN32_RC_FILE
        "Transmission Utility ('${P}')"
        "${TR_NAME}-${P}"
        "${TR_NAME}-${P}.exe")

    add_executable(${TR_NAME}-${P} ${P}.c ${${PROJECT_NAME}_${P}_WIN32_RC_FILE})
    include_directories(${TR_NAME}-${P} ${EVENT2_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
    target_link_libraries(${TR_NAME}-${P} ${TR_NAME})

    install(TARGETS ${TR_NAME}-${P} DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(INSTALL_DOC)
        install(FILES ${TR_NAME}-${P}.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()
endforeach()
