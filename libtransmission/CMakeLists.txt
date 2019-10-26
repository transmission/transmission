project(libtr)

configure_file(version.h.in version.h)

set(THIRD_PARTY_SOURCES ConvertUTF.c jsonsl.c wildmat.c)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  set(DISABLE_WARNINGS -w)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
  set(DISABLE_WARNINGS -w)
elseif(CMAKE_C_COMPILER_ID MATCHES "SunPro")
  set(DISABLE_WARNINGS -erroff)
elseif(MSVC)
  set(DISABLE_WARNINGS /w)
endif()
set_source_files_properties(${THIRD_PARTY_SOURCES} PROPERTIES COMPILE_FLAGS ${DISABLE_WARNINGS})

set(${PROJECT_NAME}_SOURCES
    announcer.c
    announcer-http.c
    announcer-udp.c
    bandwidth.c
    bitfield.c
    blocklist.c
    cache.c
    clients.c
    completion.c
    ConvertUTF.c
    crypto.c
    crypto-utils.c
    crypto-utils-cyassl.c
    crypto-utils-fallback.c
    crypto-utils-openssl.c
    crypto-utils-polarssl.c
    error.c
    fdlimit.c
    file.c
    file-posix.c
    file-win32.c
    handshake.c
    history.c
    inout.c
    list.c
    log.c
    magnet.c
    makemeta.c
    metainfo.c
    natpmp.c
    net.c
    peer-io.c
    peer-mgr.c
    peer-msgs.c
    platform.c
    platform-quota.c
    port-forwarding.c
    ptrarray.c
    quark.c
    resume.c
    rpcimpl.c
    rpc-server.c
    session.c
    session-id.c
    subprocess-posix.c
    subprocess-win32.c
    stats.c
    torrent.c
    torrent-ctor.c
    torrent-magnet.c
    tr-dht.c
    trevent.c
    tr-assert.c
    tr-getopt.c
    tr-lpd.c
    tr-udp.c
    tr-utp.c
    upnp.c
    utils.c
    variant-benc.c
    variant.c
    variant-json.c
    verify.c
    watchdir.c
    watchdir-generic.c
    watchdir-inotify.c
    watchdir-kqueue.c
    watchdir-win32.c
    web.c
    webseed.c
    wildmat.c
)

set_source_files_properties(crypto-utils-fallback.c PROPERTIES HEADER_FILE_ONLY ON)
foreach(CP cyassl openssl polarssl)
    if(NOT CP STREQUAL CRYPTO_PKG)
        set_source_files_properties(crypto-utils-${CP}.c PROPERTIES HEADER_FILE_ONLY ON)
    endif()
endforeach()

if(WITH_INOTIFY)
    add_definitions(-DWITH_INOTIFY)
else()
    set_source_files_properties(watchdir-inotify.c PROPERTIES HEADER_FILE_ONLY ON)
endif()

if(WITH_KQUEUE)
    add_definitions(-DWITH_KQUEUE)
else()
    set_source_files_properties(watchdir-kqueue.c PROPERTIES HEADER_FILE_ONLY ON)
endif()

if(WIN32)
    set_source_files_properties(file-posix.c subprocess-posix.c PROPERTIES HEADER_FILE_ONLY ON)
else()
    set_source_files_properties(file-win32.c subprocess-win32.c watchdir-win32.c PROPERTIES HEADER_FILE_ONLY ON)
endif()

set(${PROJECT_NAME}_PUBLIC_HEADERS
    error.h
    error-types.h
    file.h
    log.h
    makemeta.h
    quark.h
    rpcimpl.h
    session-id.h
    tr-assert.h
    tr-getopt.h
    tr-macros.h
    transmission.h
    utils.h
    variant.h
    watchdir.h
    web.h
    ${PROJECT_BINARY_DIR}/version.h
)

set(${PROJECT_NAME}_PRIVATE_HEADERS
    announcer-common.h
    announcer.h
    bandwidth.h
    bitfield.h
    blocklist.h
    cache.h
    clients.h
    completion.h
    ConvertUTF.h
    crypto.h
    crypto-utils.h
    fdlimit.h
    handshake.h
    history.h
    inout.h
    list.h
    magnet.h
    metainfo.h
    natpmp_local.h
    net.h
    peer-common.h
    peer-io.h
    peer-mgr.h
    peer-msgs.h
    peer-socket.h
    platform.h
    platform-quota.h
    port-forwarding.h
    ptrarray.h
    resume.h
    rpc-server.h
    session.h
    subprocess.h
    stats.h
    torrent.h
    torrent-magnet.h
    tr-dht.h
    trevent.h
    tr-lpd.h
    tr-udp.h
    tr-utp.h
    upnp.h
    variant-common.h
    verify.h
    version.h
    watchdir-common.h
    webseed.h
)

if(NOT ENABLE_UTP)
    set_source_files_properties(tr-utp.c PROPERTIES HEADER_FILE_ONLY ON)
endif()

add_definitions(
    -D__TRANSMISSION__
    "-DPACKAGE_DATA_DIR=\"${CMAKE_INSTALL_FULL_DATAROOTDIR}\""
    ${NATPMP_DEFINITIONS}
    ${MINIUPNPC_DEFINITIONS}
)

if(ENABLE_LIGHTWEIGHT)
    add_definitions(-DTR_LIGHTWEIGHT)
endif()

if(NOT ENABLE_NLS)
    add_definitions(-DDISABLE_GETTEXT)
endif()

if(ENABLE_UTP)
    add_definitions(-DWITH_UTP)
endif()

if(MINIUPNPC_VERSION VERSION_LESS 1.7)
    # API version macro was only added in 1.7
    add_definitions(-DMINIUPNPC_API_VERSION=${MINIUPNPC_API_VERSION})
endif()

if(USE_SYSTEM_B64)
    add_definitions(-DUSE_SYSTEM_B64)
endif()

if(CYASSL_IS_WOLFSSL)
    add_definitions(-DCYASSL_IS_WOLFSSL)
endif()

if(POLARSSL_IS_MBEDTLS)
    add_definitions(-DPOLARSSL_IS_MBEDTLS)
endif()

include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${CRYPTO_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${EVENT2_INCLUDE_DIRS}
    ${NATPMP_INCLUDE_DIRS}
    ${MINIUPNPC_INCLUDE_DIRS}
    ${DHT_INCLUDE_DIRS}
    ${UTP_INCLUDE_DIRS}
    ${B64_INCLUDE_DIRS}
)

if(ICONV_FOUND)
    include_directories(${ICONV_INCLUDE_DIRS})
endif()

if(ENABLE_UTP)
    include_directories(${TP_TOP}/libutp)
endif()

add_library(${TR_NAME} STATIC
    ${${PROJECT_NAME}_SOURCES}
    ${${PROJECT_NAME}_PUBLIC_HEADERS}
    ${${PROJECT_NAME}_PRIVATE_HEADERS}
)

foreach(UT ${EVENT2_UPSTREAM_TARGET}
           ${NATPMP_UPSTREAM_TARGET}
           ${MINIUPNPC_UPSTREAM_TARGET}
           ${DHT_UPSTREAM_TARGET}
           ${UTP_UPSTREAM_TARGET}
           ${B64_UPSTREAM_TARGET})
    add_dependencies(${TR_NAME} ${UT})
endforeach()

target_link_libraries(${TR_NAME}
    ${CMAKE_THREAD_LIBS_INIT}
    ${ZLIB_LIBRARIES}
    ${CRYPTO_LIBRARIES}
    ${CURL_LIBRARIES}
    ${EVENT2_LIBRARIES}
    ${NATPMP_LIBRARIES}
    ${MINIUPNPC_LIBRARIES}
    ${DHT_LIBRARIES}
    ${UTP_LIBRARIES}
    ${B64_LIBRARIES}
    ${LIBINTL_LIBRARY}
    ${LIBM_LIBRARY}
    ${TR_NETWORK_LIBRARIES}
)

if(ICONV_FOUND)
    target_link_libraries(${TR_NAME} ${ICONV_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(${TR_NAME} crypt32 shlwapi)
endif()

if(ENABLE_TESTS)
    add_library(${TR_NAME}-test STATIC
        libtransmission-test.c
        libtransmission-test.h
    )

    target_link_libraries(${TR_NAME}-test ${TR_NAME})
    set_property(TARGET ${TR_NAME}-test PROPERTY FOLDER "UnitTests")

    set(crypto-test_ADD_SOURCES crypto-test-ref.h)
    set(subprocess-test_ADD_SOURCES subprocess-test.cmd)

    set(watchdir@generic-test_DEFINITIONS WATCHDIR_TEST_FORCE_GENERIC)

    foreach(T bitfield blocklist clients crypto error file history json magnet makemeta metainfo move peer-msgs quark rename rpc
              session subprocess tr-getopt utils variant watchdir watchdir@generic)
        set(TP ${TR_NAME}-test-${T})
        if(T MATCHES "^([^@]+)@.+$")
            string(REPLACE "@" "-" TP "${TP}")
            string(REPLACE "@" "-" T_NAME "${T}")
            set(${TP}_TEST_BASENAME "${CMAKE_MATCH_1}")
        else()
            set(T_NAME "${T}")
            set(${TP}_TEST_BASENAME "${T}")
        endif()
        add_executable(${TP} ${${TP}_TEST_BASENAME}-test.c ${${T}-test_ADD_SOURCES})
        target_link_libraries(${TP} ${TR_NAME} ${TR_NAME}-test)
        if(DEFINED ${T}-test_DEFINITIONS)
            target_compile_definitions(${TP} PRIVATE ${${T}-test_DEFINITIONS})
        endif()
        add_test(NAME ${T_NAME}-test COMMAND ${TP})
        set_property(TARGET ${TP} PROPERTY FOLDER "UnitTests")
    endforeach()

    if(WIN32)
        add_custom_command(TARGET ${TR_NAME}-test-subprocess PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/subprocess-test.cmd
                $<TARGET_FILE_DIR:${TR_NAME}-test-subprocess>/${TR_NAME}-test-subprocess.cmd)
    endif()
endif()

if(INSTALL_LIB)
    install(TARGETS ${TR_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES ${${PROJECT_NAME}_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TR_NAME})
endif()
