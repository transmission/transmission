project(libtr)

configure_file(
  version.h.in
  version.h
)

set(PROJECT_FILES
  announcer.c
  announcer-http.c
  announcer-udp.c
  bandwidth.c
  bitfield.c
  blocklist.c
  cache.c
  clients.c
  completion.c
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
)

string(REPLACE ";" " " C_WARNING_FLAGS_STR "${C_WARNING_FLAGS}")
foreach(FILE ${PROJECT_FILES})
  set_source_files_properties(${FILE} PROPERTIES COMPILE_FLAGS "${C_WARNING_FLAGS_STR}")
endforeach()

set(THIRD_PARTY_FILES
  ConvertUTF.c
  jsonsl.c
  wildmat.c
)

set(${PROJECT_NAME}_SOURCES
  ${PROJECT_FILES}
  ${THIRD_PARTY_FILES}
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
)

include_directories(
    SYSTEM
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
    include_directories(SYSTEM ${ICONV_INCLUDE_DIRS})
endif()

if(ENABLE_UTP)
    include_directories(SYSTEM ${TP_TOP}/libutp)
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
