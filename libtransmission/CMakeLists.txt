project(libtr)

configure_file(version.h.in version.h)

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
    crypto-utils-fallback.c
    crypto-utils-openssl.c
    error.c
    fdlimit.c
    file.c
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
    stats.c
    torrent.c
    torrent-ctor.c
    torrent-magnet.c
    tr-dht.c
    trevent.c
    tr-getopt.c
    tr-lpd.c
    tr-udp.c
    upnp.c
    utils.c
    variant-benc.c
    variant.c
    variant-json.c
    verify.c
    web.c
    webseed.c
    wildmat.c
)

if(WIN32)
    list(APPEND ${PROJECT_NAME}_SOURCES file-win32.c)
else()
    list(APPEND ${PROJECT_NAME}_SOURCES file-posix.c)
endif()

set(${PROJECT_NAME}_PUBLIC_HEADERS
    error.h
    error-types.h
    file.h
    log.h
    makemeta.h
    quark.h
    rpcimpl.h
    tr-getopt.h
    transmission.h
    utils.h
    variant.h
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
    platform.h
    platform-quota.h
    port-forwarding.h
    ptrarray.h
    resume.h
    rpc-server.h
    session.h
    stats.h
    torrent.h
    torrent-magnet.h
    tr-dht.h
    trevent.h
    tr-lpd.h
    tr-udp.h
    upnp.h
    variant-common.h
    verify.h
    version.h
    webseed.h
)

if(ENABLE_UTP)
    list(APPEND ${PROJECT_NAME}_SOURCES tr-utp.c)
    list(APPEND ${PROJECT_NAME}_PRIVATE_HEADERS tr-utp.h)
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

include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${ICONV_INCLUDE_DIRS}
    ${EVENT2_INCLUDE_DIRS}
    ${NATPMP_INCLUDE_DIRS}
    ${MINIUPNPC_INCLUDE_DIRS}
    ${DHT_INCLUDE_DIRS}
    ${UTP_INCLUDE_DIRS}
    ${B64_INCLUDE_DIRS}
)

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
    ${OPENSSL_LIBRARIES}
    ${CURL_LIBRARIES}
    ${ICONV_LIBRARIES}
    ${EVENT2_LIBRARIES}
    ${NATPMP_LIBRARIES}
    ${MINIUPNPC_LIBRARIES}
    ${DHT_LIBRARIES}
    ${UTP_LIBRARIES}
    ${B64_LIBRARIES}
    ${LIBINTL_LIBRARY}
    ${LIBM_LIBRARY}
)

if(WIN32)
    target_link_libraries(${TR_NAME} iphlpapi ws2_32)
endif()

if(ENABLE_TESTS)
    add_library(${TR_NAME}-test STATIC
        libtransmission-test.c
        libtransmission-test.h
    )

    target_link_libraries(${TR_NAME}-test ${TR_NAME})

    set(crypto-test_ADD_SOURCES crypto-test-ref.h)

    foreach(T bitfield blocklist clients crypto error file history json magnet metainfo move peer-msgs quark rename rpc session tr-getopt utils variant)
        set(TP ${TR_NAME}-test-${T})
        add_executable(${TP} ${T}-test.c ${${T}-test_ADD_SOURCES})
        target_link_libraries(${TP} ${TR_NAME} ${TR_NAME}-test)
        add_test(${T} ${TP})
    endforeach()
endif()

if(INSTALL_LIB)
    install(TARGETS ${TR_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES ${${PROJECT_NAME}_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TR_NAME})
endif()
