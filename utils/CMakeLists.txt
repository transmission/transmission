project(trutils)

include_directories(${CMAKE_SOURCE_DIR})

foreach(P create edit show)
    add_executable(${TR_NAME}-${P} ${P}.c)
    include_directories(${TR_NAME}-${P} ${EVENT2_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
    target_link_libraries(${TR_NAME}-${P} ${TR_NAME})

    install(TARGETS ${TR_NAME}-${P} DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(INSTALL_DOC)
        install(FILES ${TR_NAME}-${P}.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()
endforeach()
