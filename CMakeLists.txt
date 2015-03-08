cmake_minimum_required(VERSION 2.8.5 FATAL_ERROR)
project(transmission)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

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
        option(ENABLE_UTILS         "Build utils (create, edit, show)" ON)
        option(ENABLE_CLI           "Build command-line client" OFF)
        option(ENABLE_TESTS         "Build unit tests" ON)
        option(ENABLE_LIGHTWEIGHT   "Optimize libtransmission for low-resource systems: smaller cache size, prefer unencrypted peer connections, etc." OFF)
        option(ENABLE_UTP           "Build µTP support" ON)
        option(ENABLE_NLS           "Enable native language support" ON)
        option(INSTALL_DOC          "Build/install documentation" ON)
        option(INSTALL_LIB          "Install the library" OFF)
        option(USE_QT5              "Use Qt 5 (instead of default Qt 4)" OFF)
tr_auto_option(USE_SYSTEM_EVENT2    "Use system event2 library" AUTO)
tr_auto_option(USE_SYSTEM_DHT       "Use system dht library" AUTO)
tr_auto_option(USE_SYSTEM_MINIUPNPC "Use system miniupnpc library" AUTO)
tr_auto_option(USE_SYSTEM_NATPMP    "Use system natpmp library" AUTO)
tr_auto_option(USE_SYSTEM_UTP       "Use system utp library" AUTO)
tr_auto_option(USE_SYSTEM_B64       "Use system b64 library" AUTO)
tr_list_option(WITH_CRYPTO          "Use specified crypto library" AUTO openssl cyassl polarssl)
tr_auto_option(WITH_INOTIFY         "Enable inotify support (on systems that support it)" AUTO)
tr_auto_option(WITH_KQUEUE          "Enable kqueue support (on systems that support it)" AUTO)
tr_auto_option(WITH_SYSTEMD         "Add support for systemd startup notification (on systems that support it)" AUTO)

set(TR_NAME ${PROJECT_NAME})

# convention: -TR MAJOR MINOR MAINT STATUS - (each a single char)
# STATUS: "X" for prerelease beta builds,
#         "Z" for unsupported trunk builds,
#         "0" for stable, supported releases
# these should be the only two lines you need to change
set(TR_USER_AGENT_PREFIX "2.84+")
set(TR_PEER_ID_PREFIX "-TR284Z-")

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

if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.svn)
    find_package(Subversion)
    if(Subversion_FOUND)
        Subversion_WC_INFO(${CMAKE_SOURCE_DIR} TR_SVN)
        set(TR_SCM_REVISION "${TR_SVN_WC_REVISION}")
    endif()

    if("${TR_SCM_REVISION}" STREQUAL "")
        file(GLOB_RECURSE TR_ALL_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} *.cc *.[chm] *.po)
        set(TR_SCM_REVISION 0)
        foreach(F ${TR_ALL_SOURCES})
            file(STRINGS ${F} F_ID REGEX "\\$Id:")
            if(F_ID MATCHES "\\$Id: [^ ]+ ([0-9]+) " AND CMAKE_MATCH_1 GREATER TR_SCM_REVISION)
                set(TR_SCM_REVISION ${CMAKE_MATCH_1})
            endif()
        endforeach()
    endif()
elseif(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.git)
    find_package(Git)
    if(GIT_FOUND)
        execute_process(
            COMMAND
                ${GIT_EXECUTABLE} rev-list --max-count=1 --abbrev-commit HEAD
            WORKING_DIRECTORY
                ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE
                TR_SCM_REVISION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()
endif()

if("${TR_SCM_REVISION}" STREQUAL "")
    set(TR_SCM_REVISION 0)
endif()

set(CURL_MINIMUM            7.15.4)
set(EVENT2_MINIMUM          2.0.10)
set(OPENSSL_MINIMUM         0.9.4)
set(CYASSL_MINIMUM          3.0)
set(POLARSSL_MINIMUM        1.2)
set(ZLIB_MINIMUM            1.2.3)
set(GTK_MINIMUM             3.4.0)
set(GLIB_MINIMUM            2.32.0)
set(GIO_MINIMUM             2.26.0)
set(LIBAPPINDICATOR_MINIMUM 0.4.90)

tr_github_upstream(EVENT2    libevent/libevent  c51b159cff 7be95856a39714dfd2218cb56415c990)
tr_github_upstream(NATPMP    miniupnp/libnatpmp 31ebda6226 7e8deb00a98220622a6f2aeb98b8921b)
tr_github_upstream(MINIUPNPC miniupnp/miniupnp  c490b42547 f9b7a4715bcd5034abae866e31102888)
tr_github_upstream(DHT       jech/dht           bf62643a95 0a2a2abe447d8a73f0084c1bc837e566)
tr_github_upstream(UTP       bittorrent/libutp  7c4f19abdf 8b92aa05abec5f6675cdde6477cd6f51)
tr_github_upstream(B64       mikedld/libb64     c1e3323498 10128cb30ad768d2c4caf627e0f69db3)

if(WIN32)
    foreach(L C CXX)
        # Target version (Vista and up)
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -DWINVER=0x0600 -D_WIN32_WINNT=0x0600")
        # Use Unicode API (although we always use W or A names explicitly)
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -DUNICODE -D_UNICODE")
        # Filter out needless definitions
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -DWIN32_LEAN_AND_MEAN -DNOMINMAX")
        # Ignore various deprecation and security warnings (at least for now)
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS")
        # Increase maximum FD_SET size
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -DFD_SETSIZE=1024")
        if(MSVC)
            # Reduce noise (at least for now)
            set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} /wd4244 /wd4267")
        endif()
    endforeach()
endif()

find_package(Threads)
find_package(PkgConfig QUIET)

find_package(CURL ${CURL_MINIMUM} REQUIRED)

if(UNIX)
    find_package(ICONV REQUIRED)
endif()

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

    set(WITH_LIBAPPINDICATOR OFF)
    if(ENABLE_GTK)
        pkg_check_modules(LIBAPPINDICATOR appindicator3-0.1>=${LIBAPPINDICATOR_MINIMUM})
        if(LIBAPPINDICATOR_FOUND)
            set(WITH_LIBAPPINDICATOR ON)
        endif()
    endif()
endif()

if(ENABLE_QT)
    tr_get_required_flag(ENABLE_QT QT_IS_REQUIRED)

    if(POLICY CMP0020)
        cmake_policy(SET CMP0020 NEW)
    endif()

    if(USE_QT5)
        set(TR_QT5_MODULES Core Gui Widgets Network DBus LinguistTools)
        set(QT_FOUND ON)
        foreach(M ${TR_QT5_MODULES})
            find_package(Qt5${M} ${QT_IS_REQUIRED})
            if(NOT Qt5${M}_FOUND)
                set(QT_FOUND OFF)
                break()
            endif()
        endforeach()
    else()
        find_package(Qt4 4.6.2 ${QT_IS_REQUIRED} COMPONENTS QtCore QtGui QtNetwork QtDBus)
    endif()

    tr_fixup_auto_option(ENABLE_QT QT_FOUND QT_IS_REQUIRED)
endif()

find_package(ZLIB ${ZLIB_MINIMUM})
if(ZLIB_FOUND)
    add_definitions(-DHAVE_ZLIB)
endif()

set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third-party)

if(WIN32)
    tr_add_external_auto_library(EVENT2 event
        PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${THIRD_PARTY_DIR}/event2.cmake" "<SOURCE_DIR>/CMakeLists.txt")
else()
    tr_add_external_auto_library(EVENT2 event
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND "<SOURCE_DIR>/autogen.sh"
                  COMMAND "<SOURCE_DIR>/configure" "--prefix=<INSTALL_DIR>" "--disable-shared")
endif()

tr_add_external_auto_library(NATPMP natpmp
    PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${THIRD_PARTY_DIR}/natpmp.cmake" "<SOURCE_DIR>/CMakeLists.txt")
if(NOT USE_SYSTEM_NATPMP)
    set(NATPMP_DEFINITIONS -DNATPMP_STATICLIB)
endif()

tr_add_external_auto_library(MINIUPNPC miniupnpc
    PATCH_COMMAND "${CMAKE_COMMAND}" -E remove_directory "<SOURCE_DIR>/minissdpd"
          COMMAND "${CMAKE_COMMAND}" -E remove_directory "<SOURCE_DIR>/miniupnpc-async"
          COMMAND "${CMAKE_COMMAND}" -E remove_directory "<SOURCE_DIR>/miniupnpc-libevent"
          COMMAND "${CMAKE_COMMAND}" -E remove_directory "<SOURCE_DIR>/miniupnpd"
          COMMAND "${CMAKE_COMMAND}" -E remove "<SOURCE_DIR>/README"
          COMMAND "${CMAKE_COMMAND}" -E copy_directory "<SOURCE_DIR>/miniupnpc" "<SOURCE_DIR>"
          COMMAND "${CMAKE_COMMAND}" -E remove_directory "<SOURCE_DIR>/miniupnpc"
    CMAKE_ARGS
        -DUPNPC_BUILD_STATIC=ON
        -DUPNPC_BUILD_SHARED=OFF
        -DUPNPC_BUILD_TESTS=OFF)
set(MINIUPNPC_DEFINITIONS -DSYSTEM_MINIUPNP)
if(NOT USE_SYSTEM_MINIUPNPC)
    list(APPEND MINIUPNPC_DEFINITIONS -DMINIUPNP_STATICLIB)
    set(MINIUPNPC_VERSION 1.9)
    set(MINIUPNPC_API_VERSION 10)
endif()

tr_add_external_auto_library(DHT dht
    PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${THIRD_PARTY_DIR}/dht.cmake" "<SOURCE_DIR>/CMakeLists.txt")

if(ENABLE_UTP)
    tr_add_external_auto_library(UTP utp
        PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${THIRD_PARTY_DIR}/utp.cmake" "<SOURCE_DIR>/CMakeLists.txt"
              COMMAND "${CMAKE_COMMAND}" -E copy "${THIRD_PARTY_DIR}/utp_config.h" "<SOURCE_DIR>/utp_config.h")
endif()

tr_add_external_auto_library(B64 b64
    PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${THIRD_PARTY_DIR}/b64.cmake" "<SOURCE_DIR>/CMakeLists.txt"
          COMMAND "${CMAKE_COMMAND}" -E chdir "<SOURCE_DIR>" patch -p1 -i "${THIRD_PARTY_DIR}/b64-01-newline.patch")

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

    pkg_check_modules(SYSTEMD_DAEMON ${SYSTEMD_IS_REQUIRED} libsystemd-daemon)

    tr_fixup_auto_option(WITH_SYSTEMD SYSTEMD_DAEMON_FOUND SYSTEMD_IS_REQUIRED)
endif()

include_directories(${CMAKE_BINARY_DIR})

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

    set(NEEDED_COMPILER_FLAGS
        -Wall
        -W
        -Wcast-align
        -Wfloat-equal
        -Wformat-security
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

set(NEEDED_HEADERS
    stdbool.h
    sys/statvfs.h
    xfs/xfs.h)

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
    canonicalize_file_name
    daemon
    fallocate64
    getmntent
    getpagesize
    htonll
    iconv_open
    localtime_r
    lseek64
    memmem
    mkdtemp
    ntohll
    posix_fadvise
    posix_fallocate
    posix_memalign
    pread
    pwrite
    statvfs
    strlcpy
    strsep
    syslog
    valloc)

foreach(F ${NEEDED_FUNCTIONS})
    tr_make_id("${F}" F_ID)
    check_function_exists(${F} HAVE_${F_ID})
    if(HAVE_${F_ID})
        add_definitions(-DHAVE_${F_ID})
    endif()
endforeach()

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

if(ENABLE_TESTS)
    enable_testing()
    include(CTest)
endif()

add_subdirectory(libtransmission)

foreach(P daemon cli utils gtk qt)
    string(TOUPPER "${P}" P_ID)
    if(ENABLE_${P_ID})
        add_subdirectory(${P})
    endif()
endforeach()

if(ENABLE_DAEMON OR ENABLE_GTK OR ENABLE_QT)
    install(DIRECTORY web DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${TR_NAME}
        PATTERN *.am EXCLUDE
        PATTERN *.scss EXCLUDE)
endif()

if(ENABLE_GTK AND ENABLE_NLS)
    find_package(Gettext REQUIRED)
    add_subdirectory(po)
endif()

if(INSTALL_DOC)
    install(FILES AUTHORS COPYING NEWS README extras/rpc-spec.txt extras/send-email-when-torrent-done.sh DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

set(CPACK_SOURCE_GENERATOR TBZ2)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${TR_NAME}-${TR_USER_AGENT_PREFIX}")
set(CPACK_SOURCE_IGNORE_FILES
    \\\\.svn
)

include(CPack)
