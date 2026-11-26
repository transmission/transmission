cmake_minimum_required(VERSION 3.25)
project(TransmissionMacWebSocketDependencies NONE)

include(ExternalProject)
find_program(XCRUN xcrun REQUIRED)
find_program(MAKE_EXECUTABLE make REQUIRED)

execute_process(
    COMMAND "${XCRUN}" --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE MACOS_SDK
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${XCRUN}" --sdk macosx --find clang
    OUTPUT_VARIABLE MACOS_CLANG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)

set(OPENSSL_REVISION c1eeb9406b6142148f267594197d853403d10208)
set(CURL_REVISION 11b991232fbcaa88e2b1faecac224416b0001e35)
set(MACOS_DEPLOYMENT_TARGET 12.0)
set(CURL_ARCHIVES)
set(SSL_ARCHIVES)
set(CRYPTO_ARCHIVES)

foreach(ARCH IN ITEMS arm64 x86_64)
    set(OPENSSL_BUILD "${CMAKE_BINARY_DIR}/build/openssl-${ARCH}")
    set(OPENSSL_PREFIX "${OPENSSL_BUILD}/install")
    set(CURL_BUILD "${CMAKE_BINARY_DIR}/build/curl-${ARCH}")
    set(CURL_PREFIX "${CURL_BUILD}/install")

    ExternalProject_Add(openssl-${ARCH}
        URL "https://codeload.github.com/openssl/openssl/tar.gz/${OPENSSL_REVISION}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${CMAKE_BINARY_DIR}/sources/openssl-${ARCH}"
        BINARY_DIR "${OPENSSL_BUILD}"
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env
            "MACOSX_DEPLOYMENT_TARGET=${MACOS_DEPLOYMENT_TARGET}"
            "SDKROOT=${MACOS_SDK}"
            "CC=${MACOS_CLANG}"
            <SOURCE_DIR>/Configure "darwin64-${ARCH}-cc"
            "--prefix=${OPENSSL_PREFIX}" --libdir=lib no-shared no-tests
        BUILD_COMMAND "${MAKE_EXECUTABLE}" -j4
        INSTALL_COMMAND "${MAKE_EXECUTABLE}" install_sw
        BUILD_BYPRODUCTS
            "${OPENSSL_PREFIX}/lib/libssl.a"
            "${OPENSSL_PREFIX}/lib/libcrypto.a")

    ExternalProject_Add(curl-${ARCH}
        URL "https://codeload.github.com/curl/curl/tar.gz/${CURL_REVISION}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${CMAKE_BINARY_DIR}/sources/curl-${ARCH}"
        BINARY_DIR "${CURL_BUILD}"
        CMAKE_GENERATOR Ninja
        CMAKE_ARGS
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_OSX_SYSROOT=${MACOS_SDK}"
            "-DCMAKE_OSX_ARCHITECTURES=${ARCH}"
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOS_DEPLOYMENT_TARGET}"
            "-DCMAKE_INSTALL_PREFIX=${CURL_PREFIX}"
            "-DOPENSSL_ROOT_DIR=${OPENSSL_PREFIX}"
            "-DOPENSSL_USE_STATIC_LIBS=ON"
            "-DBUILD_SHARED_LIBS=OFF"
            "-DBUILD_STATIC_LIBS=ON"
            "-DBUILD_CURL_EXE=OFF"
            "-DBUILD_TESTING=OFF"
            "-DCURL_USE_OPENSSL=ON"
            "-DCURL_DISABLE_WEBSOCKETS=OFF"
            "-DCURL_CA_BUNDLE=/etc/ssl/cert.pem"
            "-DCURL_USE_PKGCONFIG=OFF"
            "-DCURL_USE_LIBPSL=OFF"
            "-DCURL_USE_LIBSSH2=OFF"
            "-DUSE_NGHTTP2=OFF"
            "-DCURL_BROTLI=OFF"
            "-DCURL_ZSTD=OFF"
            "-DCURL_ZLIB=ON"
            "-DCURL_DISABLE_LDAP=ON"
            "-DCURL_USE_GSSAPI=OFF"
            "-DUSE_LIBIDN2=OFF"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel 4
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
        DEPENDS openssl-${ARCH}
        BUILD_BYPRODUCTS "${CURL_PREFIX}/lib/libcurl.a")

    list(APPEND CURL_ARCHIVES "${CURL_PREFIX}/lib/libcurl.a")
    list(APPEND SSL_ARCHIVES "${OPENSSL_PREFIX}/lib/libssl.a")
    list(APPEND CRYPTO_ARCHIVES "${OPENSSL_PREFIX}/lib/libcrypto.a")

    if(ARCH STREQUAL "arm64")
        set(ARM64_CURL_HEADERS "${CURL_PREFIX}/include/curl")
        set(ARM64_SSL_HEADERS "${OPENSSL_PREFIX}/include/openssl")
    endif()
endforeach()

set(ARCHIVE_DIR "${CMAKE_BINARY_DIR}/lib")
add_custom_command(
    OUTPUT "${ARCHIVE_DIR}/libcurl.a"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARCHIVE_DIR}"
    COMMAND "${XCRUN}" lipo -create ${CURL_ARCHIVES} -output "${ARCHIVE_DIR}/libcurl.a"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/include"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ARM64_CURL_HEADERS}" "${CMAKE_BINARY_DIR}/include/curl"
    COMMAND "${XCRUN}" lipo "${ARCHIVE_DIR}/libcurl.a" -verify_arch arm64 x86_64
    DEPENDS curl-arm64 curl-x86_64
    VERBATIM)

add_custom_command(
    OUTPUT "${ARCHIVE_DIR}/libssl.a" "${ARCHIVE_DIR}/libcrypto.a"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARCHIVE_DIR}"
    COMMAND "${XCRUN}" lipo -create ${SSL_ARCHIVES} -output "${ARCHIVE_DIR}/libssl.a"
    COMMAND "${XCRUN}" lipo -create ${CRYPTO_ARCHIVES} -output "${ARCHIVE_DIR}/libcrypto.a"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/include"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ARM64_SSL_HEADERS}" "${CMAKE_BINARY_DIR}/include/openssl"
    COMMAND "${XCRUN}" lipo "${ARCHIVE_DIR}/libssl.a" -verify_arch arm64 x86_64
    COMMAND "${XCRUN}" lipo "${ARCHIVE_DIR}/libcrypto.a" -verify_arch arm64 x86_64
    DEPENDS openssl-arm64 openssl-x86_64
    VERBATIM)

add_custom_target(webtorrent-network ALL
    DEPENDS "${ARCHIVE_DIR}/libcurl.a" "${ARCHIVE_DIR}/libssl.a" "${ARCHIVE_DIR}/libcrypto.a")
