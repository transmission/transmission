add_executable(${TR_NAME}-cli)

target_sources(${TR_NAME}-cli
    PRIVATE
        cli.cc)

target_link_libraries(${TR_NAME}-cli
    PRIVATE
        ${TR_NAME}
        fmt::fmt-header-only)

tr_win32_app_info(${TR_NAME}-cli
    "Transmission Utility ('cli', obsolete)"
    "${TR_NAME}-cli"
    "${TR_NAME}-cli.exe")

install(
    TARGETS ${TR_NAME}-cli
    DESTINATION ${CMAKE_INSTALL_BINDIR})

if(INSTALL_DOC)
    install(
        FILES ${TR_NAME}-cli.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()
