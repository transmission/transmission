project(libtr)

configure_file(
  version.h.in
  version.h
)

set(PROJECT_FILES
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
  crypto-utils-cyassl.cc
  crypto-utils-fallback.cc
  crypto-utils-openssl.cc
  crypto-utils-polarssl.cc
  crypto-utils.cc
  crypto.cc
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
  natpmp.cc
  net.cc
  open-files.cc
  peer-io.cc
  peer-mgr-active-requests.cc
  peer-mgr-wishlist.cc
  peer-mgr.cc
  peer-msgs.cc
  platform-quota.cc
  platform.cc
  port-forwarding.cc
  quark.cc
  resume.cc
  rpc-server.cc
  rpcimpl.cc
  session-id.cc
  session.cc
  stats.cc
  subprocess-posix.cc
  subprocess-win32.cc
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
  trevent.cc
  upnp.cc
  utils.cc
  variant-benc.cc
  variant-json.cc
  variant.cc
  verify.cc
  watchdir-generic.cc
  watchdir-inotify.cc
  watchdir-kqueue.cc
  watchdir-win32.cc
  watchdir.cc
  web-utils.cc
  web.cc
  webseed.cc
)

string(REPLACE ";" " " CXX_WARNING_FLAGS_STR "${CXX_WARNING_FLAGS}")
foreach(FILE ${PROJECT_FILES})
  set_source_files_properties(${FILE} PROPERTIES COMPILE_FLAGS "${CXX_WARNING_FLAGS_STR}")
endforeach()

set(THIRD_PARTY_FILES
  jsonsl.c
  wildmat.c
)

set(${PROJECT_NAME}_SOURCES
  ${PROJECT_FILES}
  ${THIRD_PARTY_FILES}
)

set_source_files_properties(crypto-utils-fallback.cc PROPERTIES HEADER_FILE_ONLY ON)
foreach(CP ccrypto cyassl openssl polarssl)
    if(NOT CP STREQUAL CRYPTO_PKG)
        set_source_files_properties(crypto-utils-${CP}.cc PROPERTIES HEADER_FILE_ONLY ON)
    endif()
endforeach()

if(WITH_INOTIFY)
    add_definitions(-DWITH_INOTIFY)
else()
    set_source_files_properties(watchdir-inotify.cc PROPERTIES HEADER_FILE_ONLY ON)
endif()

if(WITH_KQUEUE)
    add_definitions(-DWITH_KQUEUE)
else()
    set_source_files_properties(watchdir-kqueue.cc PROPERTIES HEADER_FILE_ONLY ON)
endif()

if(WIN32)
    set_source_files_properties(file-posix.cc subprocess-posix.cc PROPERTIES HEADER_FILE_ONLY ON)
else()
    set_source_files_properties(file-win32.cc subprocess-win32.cc watchdir-win32.cc PROPERTIES HEADER_FILE_ONLY ON)
endif()

if(APPLE)
    set_source_files_properties(tr-assert.cc PROPERTIES HEADER_FILE_ONLY ON)
else()
    set_source_files_properties(tr-assert.mm PROPERTIES HEADER_FILE_ONLY ON)
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
    tr-assert.h
    tr-getopt.h
    tr-macros.h
    tr-strbuf.h
    transmission.h
    utils.h
    variant.h
    watchdir.h
    web-utils.h
    web.h
)

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
    crypto.h
    file-piece-map.h
    handshake.h
    history.h
    inout.h
    lru-cache.h
    magnet-metainfo.h
    mime-types.h
    natpmp_local.h
    net.h
    open-files.h
    peer-common.h
    peer-io.h
    peer-mgr-active-requests.h
    peer-mgr-request-allocator.h
    peer-mgr-wishlist.h
    peer-mgr.h
    peer-msgs.h
    peer-socket.h
    platform-quota.h
    platform.h
    port-forwarding.h
    resume.h
    rpc-server.h
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
    tr-udp.h
    tr-utp.h
    trevent.h
    upnp.h
    variant-common.h
    verify.h
    version.h
    watchdir-common.h
    webseed.h
)

if(NOT ENABLE_UTP)
    set_source_files_properties(tr-utp.cc PROPERTIES HEADER_FILE_ONLY ON)
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
)

include_directories(
    SYSTEM
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
)

if(ICONV_FOUND)
    include_directories(SYSTEM ${ICONV_INCLUDE_DIRS})
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
           ${DEFLATE_UPSTREAM_TARGET}
           ${UTP_UPSTREAM_TARGET}
           ${PSL_UPSTREAM_TARGET}
           ${B64_UPSTREAM_TARGET})
    add_dependencies(${TR_NAME} ${UT})
endforeach()

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
    arc4::arc4
)

if(ICONV_FOUND)
    target_link_libraries(${TR_NAME} ${ICONV_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(${TR_NAME} crypt32 shlwapi)
endif()

if(APPLE)
    target_link_libraries(${TR_NAME} "-framework Foundation")
endif()

if(INSTALL_LIB)
    install(TARGETS ${TR_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES ${${PROJECT_NAME}_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TR_NAME})
endif()
