project(trcli)

include_directories(${CMAKE_SOURCE_DIR})

tr_win32_app_info(${PROJECT_NAME}_WIN32_RC_FILE
    "Transmission Utility ('cli', obsolete)"
    "${TR_NAME}-cli"
    "${TR_NAME}-cli.exe")

add_executable(${TR_NAME}-cli cli.c ${${PROJECT_NAME}_WIN32_RC_FILE})
include_directories(${TR_NAME}-cli ${CURL_INCLUDE_DIRS})
target_link_libraries(${TR_NAME}-cli ${TR_NAME})

install(TARGETS ${TR_NAME}-cli DESTINATION ${CMAKE_INSTALL_BINDIR})

if(INSTALL_DOC)
    install(FILES ${TR_NAME}-cli.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()
