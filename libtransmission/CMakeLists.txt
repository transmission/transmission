project(libtr)

include(CheckLibraryExists)
include(CheckSymbolExists)

if(ENABLE_NLS)
    check_library_exists(intl libintl_gettext "" HAVE_LIBINTL)
    if(HAVE_LIBINTL)
        set(LIBINTL_LIBRARY intl)
    endif()
endif()

check_symbol_exists(SO_REUSEPORT "sys/types.h;sys/socket.h" HAVE_SO_REUSEPORT)

configure_file(version.h.in version.h)

set(${PROJECT_NAME}_SOURCES
    announce-list.cc
    announcer-http.cc
    announcer-udp.cc
    announcer.cc
    bandwidth.cc
    bitfield.cc
    block-info.cc
    blocklist.cc
    cache.cc
    clients.cc
    completion.cc
    crypto-utils-ccrypto.cc
    crypto-utils-fallback.cc
    crypto-utils-mbedtls.cc
    crypto-utils-openssl.cc
    crypto-utils-wolfssl.cc
    crypto-utils.cc
    error.cc
    file-piece-map.cc
    file-posix.cc
    file-win32.cc
    file.cc
    handshake.cc
    inout.cc
    log.cc
    magnet-metainfo.cc
    makemeta.cc
    net.cc
    open-files.cc
    peer-io.cc
    peer-mgr-active-requests.cc
    peer-mgr-wishlist.cc
    peer-mgr.cc
    peer-mse.cc
    peer-msgs.cc
    peer-socket.cc
    platform-quota.cc
    platform.cc
    port-forwarding-natpmp.cc
    port-forwarding-upnp.cc
    port-forwarding.cc
    quark.cc
    resume.cc
    rpc-server.cc
    rpcimpl.cc
    session-id.cc
    session-alt-speeds.cc
    session-settings.cc
    session-thread.cc
    session.cc
    stats.cc
    subprocess-posix.cc
    subprocess-win32.cc
    timer-ev.cc
    torrent-ctor.cc
    torrent-files.cc
    torrent-magnet.cc
    torrent-metainfo.cc
    torrent.cc
    torrents.cc
    tr-assert.cc
    tr-assert.mm
    tr-dht.cc
    tr-getopt.cc
    tr-lpd.cc
    tr-udp.cc
    tr-utp.cc
    utils.cc
    variant-benc.cc
    variant-json.cc
    variant.cc
    variant-converters.cc
    verify.cc
    watchdir-generic.cc
    watchdir-inotify.cc
    watchdir-kqueue.cc
    watchdir-win32.cc
    watchdir.cc
    web-utils.cc
    web.cc
    webseed.cc)

set_source_files_properties(
    crypto-utils-fallback.cc
    PROPERTIES
        HEADER_FILE_ONLY ON)
foreach(CP ccrypto mbedtls openssl wolfssl)
    if(NOT CP STREQUAL CRYPTO_PKG)
        set_source_files_properties(
            crypto-utils-${CP}.cc
            PROPERTIES
                HEADER_FILE_ONLY ON)
    endif()
endforeach()

if(NOT WITH_INOTIFY)
    set_source_files_properties(
        watchdir-inotify.cc
        PROPERTIES
            HEADER_FILE_ONLY ON)
endif()

if(NOT WITH_KQUEUE)
    set_source_files_properties(
        watchdir-kqueue.cc
        PROPERTIES
            HEADER_FILE_ONLY ON)
endif()

if(WIN32)
    set_source_files_properties(
        file-posix.cc
        subprocess-posix.cc
        PROPERTIES
            HEADER_FILE_ONLY ON)
else()
    set_source_files_properties(
        file-win32.cc
        subprocess-win32.cc
        watchdir-win32.cc
        PROPERTIES
            HEADER_FILE_ONLY ON)
endif()

if(APPLE)
    set_source_files_properties(
        tr-assert.cc
        PROPERTIES
            HEADER_FILE_ONLY ON)
else()
    set_source_files_properties(
        tr-assert.mm
        PROPERTIES
            HEADER_FILE_ONLY ON)
endif()

set(${PROJECT_NAME}_PUBLIC_HEADERS
    ${PROJECT_BINARY_DIR}/version.h
    error-types.h
    error.h
    file.h
    log.h
    makemeta.h
    quark.h
    rpcimpl.h
    session-id.h
    timer-ev.h
    timer.h
    tr-assert.h
    tr-getopt.h
    tr-macros.h
    tr-strbuf.h
    tr-buffer.h
    transmission.h
    utils.h
    variant.h
    watchdir.h
    web-utils.h
    web.h)

set(${PROJECT_NAME}_PRIVATE_HEADERS
    announce-list.h
    announcer-common.h
    announcer.h
    bandwidth.h
    benc.h
    bitfield.h
    block-info.h
    blocklist.h
    cache.h
    clients.h
    completion.h
    crypto-utils.h
    file-piece-map.h
    handshake.h
    history.h
    inout.h
    lru-cache.h
    magnet-metainfo.h
    mime-types.h
    net.h
    open-files.h
    peer-common.h
    peer-io.h
    peer-mgr-active-requests.h
    peer-mgr-wishlist.h
    peer-mgr.h
    peer-mse.h
    peer-msgs.h
    peer-socket.h
    platform-quota.h
    platform.h
    port-forwarding-natpmp.h
    port-forwarding-upnp.h
    port-forwarding.h
    resume.h
    rpc-server.h
    session-alt-speeds.h
    session-thread.h
    session.h
    stats.h
    subprocess.h
    torrent-files.h
    torrent-magnet.h
    torrent-metainfo.h
    torrent.h
    torrents.h
    tr-dht.h
    tr-lpd.h
    tr-utp.h
    utils-ev.h
    variant-common.h
    verify.h
    version.h
    watchdir-base.h
    webseed.h)

if(NOT ENABLE_UTP)
    set_source_files_properties(
        tr-utp.cc
        PROPERTIES
            HEADER_FILE_ONLY ON)
endif()

add_library(${TR_NAME} STATIC)

target_sources(${TR_NAME}
    PRIVATE
        ${${PROJECT_NAME}_SOURCES}
        ${${PROJECT_NAME}_PUBLIC_HEADERS}
        ${${PROJECT_NAME}_PRIVATE_HEADERS})

foreach(UT
        ${EVENT2_UPSTREAM_TARGET}
        ${NATPMP_UPSTREAM_TARGET}
        ${MINIUPNPC_UPSTREAM_TARGET}
        ${DHT_UPSTREAM_TARGET}
        ${DEFLATE_UPSTREAM_TARGET}
        ${UTP_UPSTREAM_TARGET}
        ${PSL_UPSTREAM_TARGET}
        ${B64_UPSTREAM_TARGET})
    add_dependencies(${TR_NAME} ${UT})
endforeach()

target_compile_definitions(${TR_NAME}
    PRIVATE
        __TRANSMISSION__
        WIDE_INTEGER_DISABLE_FLOAT_INTEROP
        WIDE_INTEGER_DISABLE_IOSTREAM
        PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATAROOTDIR}"
        $<$<BOOL:${WITH_INOTIFY}>:WITH_INOTIFY>
        $<$<BOOL:${WITH_KQUEUE}>:WITH_KQUEUE>
        $<$<BOOL:${ENABLE_LIGHTWEIGHT}>:TR_LIGHTWEIGHT>
        $<$<BOOL:${ENABLE_UTP}>:WITH_UTP>
        $<$<VERSION_LESS:${MINIUPNPC_VERSION},1.7>:MINIUPNPC_API_VERSION=${MINIUPNPC_API_VERSION}> # API version macro was only added in 1.7
        $<$<BOOL:${USE_SYSTEM_B64}>:USE_SYSTEM_B64>
        $<$<BOOL:${HAVE_SO_REUSEPORT}>:HAVE_SO_REUSEPORT=1>
        $<$<BOOL:${ICONV_FOUND}>:HAVE_ICONV>
        $<$<BOOL:${ICONV_SECOND_ARGUMENT_IS_CONST}>:ICONV_SECOND_ARGUMENT_IS_CONST>
        ${NATPMP_DEFINITIONS}
        ${MINIUPNPC_DEFINITIONS}
    PUBLIC
        $<$<NOT:$<BOOL:${ENABLE_NLS}>>:DISABLE_GETTEXT>)

tr_target_compile_definitions_for_headers(${TR_NAME}
    PRIVATE
        sys/statvfs.h
        xfs/xfs.h
        xlocale.h)

if(ENABLE_NLS)
    tr_target_compile_definitions_for_headers(${TR_NAME}
        PRIVATE
            libintl.h)
endif()

tr_target_compile_definitions_for_functions(${TR_NAME}
    PRIVATE
        copyfile
        copy_file_range
        fallocate64
        flock
        getmntent
        htonll
        mkdtemp
        ntohll
        posix_fadvise
        posix_fallocate
        pread
        pwrite
        sendfile64
        statvfs
    PUBLIC
        gettext
        ngettext
    REQUIRED_LIBS
        $<$<BOOL:${HAVE_LIBINTL}>:${LIBINTL_LIBRARY}>) # gettext, ngettext

target_include_directories(${TR_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_BINARY_DIR}
    INTERFACE
        ${CMAKE_BINARY_DIR})

target_include_directories(${TR_NAME} SYSTEM
    PRIVATE
        ${WIDE_INTEGER_INCLUDE_DIRS}
        ${UTFCPP_INCLUDE_DIRS}
        ${DEFLATE_INCLUDE_DIRS}
        ${FAST_FLOAT_INCLUDE_DIRS}
        ${CRYPTO_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
        ${EVENT2_INCLUDE_DIRS}
        ${PSL_INCLUDE_DIRS}
        ${NATPMP_INCLUDE_DIRS}
        ${MINIUPNPC_INCLUDE_DIRS}
        ${DHT_INCLUDE_DIRS}
        ${UTP_INCLUDE_DIRS}
        ${B64_INCLUDE_DIRS}
        $<$<BOOL:${ICONV_FOUND}>:${ICONV_INCLUDE_DIRS}>
    PUBLIC
        ${LIBFMT_INCLUDE_DIRS})

target_link_libraries(${TR_NAME}
    ${CMAKE_THREAD_LIBS_INIT}
    ${DEFLATE_LIBRARIES}
    ${CRYPTO_LIBRARIES}
    ${CURL_LIBRARIES}
    ${EVENT2_LIBRARIES}
    ${PSL_LIBRARIES}
    ${NATPMP_LIBRARIES}
    ${MINIUPNPC_LIBRARIES}
    ${DHT_LIBRARIES}
    ${UTP_LIBRARIES}
    ${B64_LIBRARIES}
    ${LIBINTL_LIBRARY}
    ${LIBM_LIBRARY}
    ${TR_NETWORK_LIBRARIES}
    jsonsl
    wildmat)

if(ICONV_FOUND)
    target_link_libraries(${TR_NAME}
        ${ICONV_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(${TR_NAME}
        crypt32
        shlwapi)
endif()

if(APPLE)
    target_link_libraries(${TR_NAME}
        "-framework Foundation")
endif()

if(INSTALL_LIB)
    install(
        TARGETS ${TR_NAME}
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(
        FILES ${${PROJECT_NAME}_PUBLIC_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TR_NAME})
endif()
