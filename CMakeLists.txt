cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)
project(transmission)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

set(CMAKE_MACOSX_RPATH ON)

include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(ExternalProject)
include(GNUInstallDirs)
include(TrMacros)

        option(ENABLE_DAEMON        "Build daemon" ON)
tr_auto_option(ENABLE_GTK           "Build GTK+ client" AUTO)
tr_auto_option(ENABLE_QT            "Build Qt client" AUTO)
tr_auto_option(ENABLE_MAC           "Build Mac client" AUTO)
        option(ENABLE_UTILS         "Build utils (create, edit, show)" ON)
        option(ENABLE_CLI           "Build command-line client" OFF)
        option(ENABLE_TESTS         "Build unit tests" ON)
        option(ENABLE_LIGHTWEIGHT   "Optimize libtransmission for low-resource systems: smaller cache size, prefer unencrypted peer connections, etc." OFF)
        option(ENABLE_UTP           "Build µTP support" ON)
        option(ENABLE_NLS           "Enable native language support" ON)
        option(INSTALL_DOC          "Build/install documentation" ON)
        option(INSTALL_LIB          "Install the library" OFF)
tr_auto_option(USE_SYSTEM_EVENT2    "Use system event2 library" AUTO)
tr_auto_option(USE_SYSTEM_DHT       "Use system dht library" AUTO)
tr_auto_option(USE_SYSTEM_MINIUPNPC "Use system miniupnpc library" AUTO)
tr_auto_option(USE_SYSTEM_NATPMP    "Use system natpmp library" AUTO)
tr_auto_option(USE_SYSTEM_UTP       "Use system utp library" AUTO)
tr_auto_option(USE_SYSTEM_B64       "Use system b64 library" AUTO)
tr_list_option(WITH_CRYPTO          "Use specified crypto library" AUTO openssl cyassl polarssl)
tr_auto_option(WITH_INOTIFY         "Enable inotify support (on systems that support it)" AUTO)
tr_auto_option(WITH_KQUEUE          "Enable kqueue support (on systems that support it)" AUTO)
tr_auto_option(WITH_LIBAPPINDICATOR "Use libappindicator in GTK+ client" AUTO)
tr_auto_option(WITH_SYSTEMD         "Add support for systemd startup notification (on systems that support it)" AUTO)

set(TR_NAME ${PROJECT_NAME})

# convention: -TR MAJOR MINOR MAINT STATUS - (each a single char)
# STATUS: "X" for prerelease beta builds,
#         "Z" for unsupported trunk builds,
#         "0" for stable, supported releases
# these should be the only two lines you need to change
set(TR_USER_AGENT_PREFIX "3.00")
set(TR_PEER_ID_PREFIX "-TR3000-")

string(REGEX MATCH "^([0-9]+)\\.([0-9]+).*" TR_VERSION "${TR_USER_AGENT_PREFIX}")
set(TR_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(TR_VERSION_MINOR "${CMAKE_MATCH_2}")

if(TR_PEER_ID_PREFIX MATCHES "X-$")
    set(TR_BETA_RELEASE 1)
elseif(TR_PEER_ID_PREFIX MATCHES "Z-$")
    set(TR_NIGHTLY_RELEASE 1)
else()
    set(TR_STABLE_RELEASE 1)
endif()

set(TR_VCS_REVISION_FILE "${CMAKE_SOURCE_DIR}/REVISION")

if(NOT "$ENV{JENKINS_URL}" STREQUAL "" AND NOT "$ENV{GIT_COMMIT}" STREQUAL "")
    set(TR_VCS_REVISION "$ENV{GIT_COMMIT}")
elseif(NOT "$ENV{TEAMCITY_PROJECT_NAME}" STREQUAL "" AND NOT "$ENV{BUILD_VCS_NUMBER}" STREQUAL "")
    set(TR_VCS_REVISION "$ENV{BUILD_VCS_NUMBER}")
elseif(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.git)
    find_package(Git)
    if(GIT_FOUND)
        execute_process(
            COMMAND
                ${GIT_EXECUTABLE} rev-list --max-count=1 HEAD
            WORKING_DIRECTORY
                ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE
                TR_VCS_REVISION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()
endif()

if("${TR_VCS_REVISION}" STREQUAL "" AND EXISTS "${TR_VCS_REVISION_FILE}")
    file(READ "${TR_VCS_REVISION_FILE}" TR_VCS_REVISION)
endif()

string(STRIP "${TR_VCS_REVISION}" TR_VCS_REVISION)

if(NOT "${TR_VCS_REVISION}" STREQUAL "")
    file(WRITE "${TR_VCS_REVISION_FILE}" "${TR_VCS_REVISION}\n")
else()
    set(TR_VCS_REVISION 0)
    file(REMOVE "${TR_VCS_REVISION_FILE}")
endif()

string(SUBSTRING "${TR_VCS_REVISION}" 0 10 TR_VCS_REVISION)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CURL_MINIMUM            7.15.4)
set(EVENT2_MINIMUM          2.0.10)
set(OPENSSL_MINIMUM         0.9.7)
set(CYASSL_MINIMUM          3.0)
set(POLARSSL_MINIMUM        1.2)
set(ZLIB_MINIMUM            1.2.3)
set(GTK_MINIMUM             3.4.0)
set(GLIB_MINIMUM            2.32.0)
set(GIO_MINIMUM             2.26.0)
set(LIBAPPINDICATOR_MINIMUM 0.4.90)
set(QT5_MINIMUM             5.2)

if(WIN32)
    foreach(L C CXX)
        # Target version (Vista and up)
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -DWINVER=0x0600 -D_WIN32_WINNT=0x0600")
        # Use Unicode API (although we always use W or A names explicitly)
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -DUNICODE -D_UNICODE")
        # Ignore various deprecation and security warnings (at least for now)
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS")
        # Increase maximum FD_SET size
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -DFD_SETSIZE=1024")
        if(MSVC)
            # Reduce noise (at least for now)
            set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} /wd4244 /wd4267")
        endif()

        if(MINGW)
            set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -D__USE_MINGW_ANSI_STDIO=1")
        endif()
    endforeach()
endif()

find_package(Threads)
find_package(PkgConfig QUIET)

find_package(CURL ${CURL_MINIMUM} REQUIRED)

find_package(ICONV)

set(CRYPTO_PKG "")
if(WITH_CRYPTO STREQUAL "AUTO" OR WITH_CRYPTO STREQUAL "openssl")
    tr_get_required_flag(WITH_CRYPTO OPENSSL_IS_REQUIRED)
    find_package(OpenSSL ${OPENSSL_MINIMUM} ${OPENSSL_IS_REQUIRED})
    tr_fixup_list_option(WITH_CRYPTO "openssl" OPENSSL_FOUND "AUTO" OPENSSL_IS_REQUIRED)
    if(WITH_CRYPTO STREQUAL "openssl")
        set(CRYPTO_PKG "openssl")
        set(CRYPTO_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
        set(CRYPTO_LIBRARIES ${OPENSSL_LIBRARIES})
    endif()
endif()
if(WITH_CRYPTO STREQUAL "AUTO" OR WITH_CRYPTO STREQUAL "cyassl")
    tr_get_required_flag(WITH_CRYPTO CYASSL_IS_REQUIRED)
    find_package(CyaSSL ${CYASSL_MINIMUM} ${CYASSL_IS_REQUIRED})
    tr_fixup_list_option(WITH_CRYPTO "cyassl" CYASSL_FOUND "AUTO" CYASSL_IS_REQUIRED)
    if(WITH_CRYPTO STREQUAL "cyassl")
        set(CRYPTO_PKG "cyassl")
        set(CRYPTO_INCLUDE_DIRS ${CYASSL_INCLUDE_DIRS})
        set(CRYPTO_LIBRARIES ${CYASSL_LIBRARIES})
    endif()
endif()
if(WITH_CRYPTO STREQUAL "AUTO" OR WITH_CRYPTO STREQUAL "polarssl")
    tr_get_required_flag(WITH_CRYPTO POLARSSL_IS_REQUIRED)
    find_package(PolarSSL ${POLARSSL_MINIMUM} ${POLARSSL_IS_REQUIRED})
    tr_fixup_list_option(WITH_CRYPTO "polarssl" POLARSSL_FOUND "AUTO" POLARSSL_IS_REQUIRED)
    if(WITH_CRYPTO STREQUAL "polarssl")
        set(CRYPTO_PKG "polarssl")
        set(CRYPTO_INCLUDE_DIRS ${POLARSSL_INCLUDE_DIRS})
        set(CRYPTO_LIBRARIES ${POLARSSL_LIBRARIES})
    endif()
endif()
# We should have found the library by now
if(CRYPTO_PKG STREQUAL "")
    if(WITH_CRYPTO STREQUAL "AUTO")
        message(FATAL_ERROR "Unable to find any supported crypto library.")
    else()
        message(FATAL_ERROR "Requested crypto library '${WITH_CRYPTO}' is not supported.")
    endif()
endif()

if(ENABLE_GTK)
    tr_get_required_flag(ENABLE_GTK GTK_IS_REQUIRED)

    pkg_check_modules(GTK ${GTK_IS_REQUIRED}
        gtk+-3.0>=${GTK_MINIMUM}
        glib-2.0>=${GLIB_MINIMUM}
        gio-2.0>=${GIO_MINIMUM}
        gmodule-2.0>=${GLIB_MINIMUM}
        gthread-2.0>=${GLIB_MINIMUM})

    tr_fixup_auto_option(ENABLE_GTK GTK_FOUND GTK_IS_REQUIRED)

    if(ENABLE_GTK AND WITH_LIBAPPINDICATOR)
        tr_get_required_flag(WITH_LIBAPPINDICATOR LIBAPPINDICATOR_IS_REQUIRED)
        pkg_check_modules(LIBAPPINDICATOR appindicator3-0.1>=${LIBAPPINDICATOR_MINIMUM})
        tr_fixup_auto_option(WITH_LIBAPPINDICATOR LIBAPPINDICATOR_FOUND LIBAPPINDICATOR_IS_REQUIRED)
    endif()
else()
    set(WITH_LIBAPPINDICATOR OFF)
endif()

if(ENABLE_QT)
    tr_get_required_flag(ENABLE_QT QT_IS_REQUIRED)

    if(POLICY CMP0020)
        cmake_policy(SET CMP0020 NEW)
    endif()

    set(QT_TARGETS)
    set(ENABLE_QT_COM_INTEROP OFF)
    set(ENABLE_QT_DBUS_INTEROP OFF)

    set(QT5_REQUIRED_MODULES Core Gui Widgets Network LinguistTools)
    set(QT5_OPTIONAL_MODULES DBus AxContainer AxServer)

    if(WIN32)
        list(APPEND QT5_REQUIRED_MODULES WinExtras)
    endif()

    foreach(M ${QT5_REQUIRED_MODULES})
        find_package(Qt5${M} ${QT5_MINIMUM} QUIET)
        if(Qt5${M}_FOUND)
            if(NOT M STREQUAL "LinguistTools")
                list(APPEND QT_TARGETS Qt5::${M})
            endif()
        else()
            set(QT_TARGETS)
            break()
        endif()
    endforeach()

    if(QT_TARGETS)
        foreach(M ${QT5_OPTIONAL_MODULES})
            find_package(Qt5${M} ${QT5_MINIMUM} QUIET)
            if(Qt5${M}_FOUND)
                list(APPEND QT_TARGETS Qt5::${M})
            endif()
        endforeach()

        if(Qt5AxContainer_FOUND AND Qt5AxServer_FOUND)
            set(ENABLE_QT_COM_INTEROP ON)

            find_program(MIDL_EXECUTABLE midl)
            if(NOT MIDL_EXECUTABLE)
                set(ENABLE_QT_COM_INTEROP OFF)
            endif()
        endif()

        if(Qt5DBus_FOUND)
            set(ENABLE_QT_DBUS_INTEROP ON)
        endif()
    endif()

    set(QT_FOUND ON)
    if(NOT QT_TARGETS OR NOT (ENABLE_QT_COM_INTEROP OR ENABLE_QT_DBUS_INTEROP))
        if(QT_IS_REQUIRED)
            message(FATAL_ERROR "Unable to find required Qt libraries.")
        endif()
        set(QT_FOUND OFF)
    endif()

    tr_fixup_auto_option(ENABLE_QT QT_FOUND QT_IS_REQUIRED)
endif()

if(ENABLE_MAC)
    tr_get_required_flag(ENABLE_MAC MAC_IS_REQUIRED)

    if(APPLE)
        set(MAC_FOUND ON)
    else()
        set(MAC_FOUND OFF)
        if(MAC_IS_REQUIRED)
            message(SEND_ERROR "Mac build is impossible on non-Mac system.")
        endif()
    endif()

    tr_fixup_auto_option(ENABLE_MAC MAC_FOUND MAC_IS_REQUIRED)
endif()

find_package(ZLIB ${ZLIB_MINIMUM})
if(ZLIB_FOUND)
    add_definitions(-DHAVE_ZLIB)
endif()

set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third-party)

if(WIN32)
    tr_add_external_auto_library(EVENT2 libevent event)
else()
    set(EVENT2_CONFIGURE_FLAGS)
    if(APPLE)
        if(CRYPTO_PKG STREQUAL "openssl")
            list(APPEND EVENT2_CONFIGURE_FLAGS "CPPFLAGS=-I${CRYPTO_INCLUDE_DIRS}")
        else()
            list(APPEND EVENT2_CONFIGURE_FLAGS "CPPFLAGS=-I${THIRD_PARTY_DIR}/openssl/include")
        endif()
    endif()
    tr_add_external_auto_library(EVENT2 libevent event
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND "<SOURCE_DIR>/autogen.sh"
                  COMMAND "<SOURCE_DIR>/configure" "--prefix=<INSTALL_DIR>" "--disable-shared" ${EVENT2_CONFIGURE_FLAGS})
endif()

tr_add_external_auto_library(NATPMP libnatpmp natpmp)
if(NOT USE_SYSTEM_NATPMP)
    set(NATPMP_DEFINITIONS -DNATPMP_STATICLIB)
endif()

tr_add_external_auto_library(MINIUPNPC miniupnpc miniupnpc
    CMAKE_ARGS
        -DUPNPC_BUILD_STATIC=ON
        -DUPNPC_BUILD_SHARED=OFF
        -DUPNPC_BUILD_TESTS=OFF)
set(MINIUPNPC_DEFINITIONS -DSYSTEM_MINIUPNP)
if(NOT USE_SYSTEM_MINIUPNPC)
    list(APPEND MINIUPNPC_DEFINITIONS -DMINIUPNP_STATICLIB)
    set(MINIUPNPC_VERSION 1.9)
    set(MINIUPNPC_API_VERSION 12)
endif()

tr_add_external_auto_library(DHT dht dht)

if(ENABLE_UTP)
    tr_add_external_auto_library(UTP libutp utp)

    if(UTP_UPSTREAM_TARGET)
        # Use C++ linker for anything that depends on static libutp
        # TODO: switch to imported targets for all the dependencies
        add_library(UTP::UTP STATIC IMPORTED)
        set_property(TARGET UTP::UTP PROPERTY IMPORTED_LOCATION "${UTP_LIBRARIES}")
        set_property(TARGET UTP::UTP PROPERTY IMPORTED_LINK_INTERFACE_LANGUAGES "CXX")
        set(UTP_LIBRARIES UTP::UTP)
    endif()
endif()

tr_add_external_auto_library(B64 libb64 b64)

if(WITH_INOTIFY)
    tr_get_required_flag(WITH_INOTIFY INOTIFY_IS_REQUIRED)

    set(INOTIFY_FOUND OFF)
    check_include_file(sys/inotify.h HAVE_SYS_INOTIFY_H)
    check_function_exists(inotify_init HAVE_INOTIFY_INIT)
    if(HAVE_SYS_INOTIFY_H AND HAVE_INOTIFY_INIT)
        set(INOTIFY_FOUND ON)
    endif()

    tr_fixup_auto_option(WITH_INOTIFY INOTIFY_FOUND INOTIFY_IS_REQUIRED)
endif()

if(WITH_KQUEUE)
    tr_get_required_flag(WITH_KQUEUE KQUEUE_IS_REQUIRED)

    set(KQUEUE_FOUND OFF)
    check_include_files("sys/types.h;sys/event.h" HAVE_SYS_EVENT_H)
    check_function_exists(kqueue HAVE_KQUEUE)
    if(HAVE_SYS_EVENT_H AND HAVE_KQUEUE)
        set(KQUEUE_FOUND ON)
    endif()

    tr_fixup_auto_option(WITH_KQUEUE KQUEUE_FOUND KQUEUE_IS_REQUIRED)
endif()

if(WITH_SYSTEMD)
    tr_get_required_flag(WITH_SYSTEMD SYSTEMD_IS_REQUIRED)
    find_package(SYSTEMD)
    tr_fixup_auto_option(WITH_SYSTEMD SYSTEMD_FOUND SYSTEMD_IS_REQUIRED)
endif()

include_directories(${CMAKE_BINARY_DIR})

if(WIN32)
    foreach(L C CXX)
        # Filter out needless definitions
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -DWIN32_LEAN_AND_MEAN -DNOMINMAX")
    endforeach()
endif()

if(CMAKE_VERSION VERSION_LESS "3.1")
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17")
    endif()
else()
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(NEEDED_COMPILER_FLAGS
        -Wall
        -W
        -Wcast-align
        -Wfloat-equal
        -Wmissing-format-attribute
        -Wpointer-arith
        -Wredundant-decls
        -Wundef
        -Wunused-parameter
        -Wwrite-strings)

    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_VERSION VERSION_GREATER "3.3")
        list(APPEND NEEDED_COMPILER_FLAGS
            -Wextra
            -Winit-self)
    endif()

    if(MINGW)
        # Disable excessive warnings since we're using __USE_MINGW_ANSI_STDIO
        # Hopefully, any potential issues will be spotted on other platforms
        list(APPEND NEEDED_COMPILER_FLAGS -Wno-format)
    else()
        list(APPEND NEEDED_COMPILER_FLAGS -Wformat-security)
    endif()

    set(NEEDED_C_COMPILER_FLAGS
        ${NEEDED_COMPILER_FLAGS}
        -Winline
        -Wmissing-declarations
        -Wnested-externs
        -Wstrict-prototypes)
    string(REPLACE ";" " " NEEDED_C_COMPILER_FLAGS_STRING "${NEEDED_C_COMPILER_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${NEEDED_C_COMPILER_FLAGS_STRING}")

    set(NEEDED_CXX_COMPILER_FLAGS
        ${NEEDED_COMPILER_FLAGS})
    string(REPLACE ";" " " NEEDED_CXX_COMPILER_FLAGS_STRING "${NEEDED_CXX_COMPILER_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${NEEDED_CXX_COMPILER_FLAGS_STRING}")
endif()

include(LargeFileSupport)

set(NEEDED_HEADERS
    sys/statvfs.h
    xfs/xfs.h
    xlocale.h)

if(ENABLE_NLS)
    list(APPEND NEEDED_HEADERS libintl.h)
endif()

foreach(H ${NEEDED_HEADERS})
    tr_make_id("${H}" H_ID)
    check_include_file(${H} HAVE_${H_ID})
    if(HAVE_${H_ID})
        add_definitions(-DHAVE_${H_ID})
    endif()
endforeach()

set(NEEDED_FUNCTIONS
    _configthreadlocale
    canonicalize_file_name
    daemon
    fallocate64
    flock
    getmntent
    getpagesize
    htonll
    localtime_r
    memmem
    mkdtemp
    ntohll
    posix_fadvise
    posix_fallocate
    posix_memalign
    pread
    pwrite
    statvfs
    strcasestr
    strlcpy
    strsep
    syslog
    uselocale
    valloc)

foreach(F ${NEEDED_FUNCTIONS})
    tr_make_id("${F}" F_ID)
    check_function_exists(${F} HAVE_${F_ID})
    if(HAVE_${F_ID})
        add_definitions(-DHAVE_${F_ID})
    endif()
endforeach()

if(ICONV_FOUND)
    add_definitions(-DHAVE_ICONV)
    if(ICONV_SECOND_ARGUMENT_IS_CONST)
        add_definitions(-DICONV_SECOND_ARGUMENT_IS_CONST)
    endif()
endif()

# if(MINGW)
#     check_function_exists(__mingw_printf HAVE_MINGW_PRINTF)
#     if(HAVE_MINGW_PRINTF)
#         add_definitions(-D__USE_MINGW_ANSI_STDIO=1 -D__STDC_FORMAT_MACROS=1)
#     endif()
# endif()

if(ENABLE_NLS)
    check_library_exists(intl libintl_gettext "" HAVE_LIBINTL)
    if(HAVE_LIBINTL)
        set(LIBINTL_LIBRARY intl)
    endif()
endif()

check_library_exists(m sqrt "" HAVE_LIBM)
if(HAVE_LIBM)
    set(LIBM_LIBRARY m)
endif()

set(TR_NETWORK_LIBRARIES)
if(WIN32)
    list(APPEND TR_NETWORK_LIBRARIES iphlpapi ws2_32)
else()
    tr_select_library("c;socket;net" socket "" LIB)
    if(NOT LIB MATCHES "^(|c)$")
        list(APPEND TR_NETWORK_LIBRARIES ${LIB})
    endif()

    tr_select_library("c;nsl;bind" gethostbyname "" LIB)
    if(NOT LIB MATCHES "^(|c)$")
        list(APPEND TR_NETWORK_LIBRARIES ${LIB})
    endif()
endif()

if(ENABLE_TESTS)
    enable_testing()
endif()

function(tr_install_web DST_DIR)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/web DESTINATION ${DST_DIR}
        PATTERN *.am EXCLUDE
        PATTERN *.in EXCLUDE
        PATTERN *.scss EXCLUDE)
endfunction()

add_subdirectory(libtransmission)

set(MAC_PROJECT_DIR macosx)

if(ENABLE_GTK AND ENABLE_NLS)
    find_package(Gettext 0.19.7 REQUIRED)
    add_subdirectory(po)
endif()

foreach(P daemon cli utils gtk qt mac)
    string(TOUPPER "${P}" P_ID)
    if(ENABLE_${P_ID})
        if(DEFINED ${P_ID}_PROJECT_DIR)
            set(P ${${P_ID}_PROJECT_DIR})
        endif()
        add_subdirectory(${P})
    endif()
endforeach()

if(ENABLE_DAEMON OR ENABLE_GTK OR ENABLE_QT)
    tr_install_web(${CMAKE_INSTALL_DATAROOTDIR}/${TR_NAME})
endif()

if(INSTALL_DOC)
    install(FILES AUTHORS COPYING NEWS.md README.md extras/rpc-spec.txt extras/send-email-when-torrent-done.sh DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

if(MSVC AND ENABLE_DAEMON AND ENABLE_QT AND ENABLE_UTILS AND WITH_CRYPTO STREQUAL "openssl")
    add_subdirectory(dist/msi)
endif()

set(CPACK_SOURCE_GENERATOR TBZ2)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${TR_NAME}-${TR_USER_AGENT_PREFIX}")
set(CPACK_SOURCE_IGNORE_FILES
    \\\\.git
)

include(CPack)
