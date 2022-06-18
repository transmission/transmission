cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

if(POLICY CMP0092)
    cmake_policy(SET CMP0092 NEW)
endif()

# The value of this variable should be set prior to the first project() command invocation.
# See: https://cmake.org/cmake/help/latest/variable/CMAKE_OSX_DEPLOYMENT_TARGET.html
if(CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
endif()

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
        option(ENABLE_WEB           "Build Web client" OFF)
        option(ENABLE_UTILS         "Build utils (create, edit, show)" ON)
        option(ENABLE_CLI           "Build command-line client" OFF)
        option(ENABLE_TESTS         "Build unit tests" ON)
        option(ENABLE_LIGHTWEIGHT   "Optimize libtransmission for low-resource systems: smaller cache size, prefer unencrypted peer connections, etc." OFF)
        option(ENABLE_UTP           "Build µTP support" ON)
        option(ENABLE_NLS           "Enable native language support" ON)
        option(INSTALL_DOC          "Build/install documentation" ON)
        option(INSTALL_LIB          "Install the library" OFF)
tr_auto_option(RUN_CLANG_TIDY       "Run clang-tidy on the code" AUTO)
tr_auto_option(USE_SYSTEM_EVENT2    "Use system event2 library" AUTO)
tr_auto_option(USE_SYSTEM_DEFLATE   "Use system deflate library" AUTO)
tr_auto_option(USE_SYSTEM_DHT       "Use system dht library" AUTO)
tr_auto_option(USE_SYSTEM_MINIUPNPC "Use system miniupnpc library" AUTO)
tr_auto_option(USE_SYSTEM_NATPMP    "Use system natpmp library" AUTO)
tr_auto_option(USE_SYSTEM_UTP       "Use system utp library" AUTO)
tr_auto_option(USE_SYSTEM_B64       "Use system b64 library" AUTO)
tr_auto_option(USE_SYSTEM_PSL       "Use system psl library" AUTO)
tr_list_option(USE_QT_VERSION       "Use specific Qt version" AUTO 5 6)
tr_list_option(WITH_CRYPTO          "Use specified crypto library" AUTO openssl cyassl polarssl ccrypto)
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
set(TR_USER_AGENT_PREFIX "3.00+")
set(TR_PEER_ID_PREFIX "-TR300Z-")

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

if(EXISTS ${CMAKE_SOURCE_DIR}/.git)
    find_package(Git)
endif()

if(NOT "$ENV{JENKINS_URL}" STREQUAL "" AND NOT "$ENV{GIT_COMMIT}" STREQUAL "")
    set(TR_VCS_REVISION "$ENV{GIT_COMMIT}")
elseif(NOT "$ENV{TEAMCITY_PROJECT_NAME}" STREQUAL "" AND NOT "$ENV{BUILD_VCS_NUMBER}" STREQUAL "")
    set(TR_VCS_REVISION "$ENV{BUILD_VCS_NUMBER}")
elseif(GIT_FOUND)
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

set(PSL_MINIMUM             0.21.1)
set(CURL_MINIMUM            7.28.0)
set(CYASSL_MINIMUM          3.0)
set(DEFLATE_MINIMUM         1.10)
set(EVENT2_MINIMUM          2.1.0)
set(GIO_MINIMUM             2.26.0)
set(GLIB_MINIMUM            2.50.1)
set(GTK_MINIMUM             3.24.0)
set(LIBAPPINDICATOR_MINIMUM 0.4.90)
set(OPENSSL_MINIMUM         0.9.7)
set(POLARSSL_MINIMUM        1.2)
set(QT_MINIMUM              5.6)

if(WIN32)
    foreach(L C CXX)
        set(CMAKE_${L}_FLAGS "${CMAKE_${L}_FLAGS} -DWIN32")
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

find_package(Fmt)
add_definitions(-DFMT_HEADER_ONLY -DFMT_EXCEPTIONS=0)
include_directories(SYSTEM ${LIBFMT_INCLUDE_DIRS})

find_package(FastFloat)
find_package(UtfCpp)
find_package(Threads)
find_package(PkgConfig QUIET)

find_package(CURL ${CURL_MINIMUM} REQUIRED)
if(TARGET CURL::libcurl)
    set(CURL_LIBRARIES CURL::libcurl)
    set(CURL_INCLUDE_DIRS)
endif()

find_package(ICONV)

set(CRYPTO_PKG "")
if(WITH_CRYPTO STREQUAL "AUTO" OR WITH_CRYPTO STREQUAL "ccrypto")
    tr_get_required_flag(WITH_CRYPTO CCRYPTO_IS_REQUIRED)
    find_path(CCRYPTO_INCLUDE_DIR NAMES CommonCrypto/CommonCrypto.h ${CCRYPTO_IS_REQUIRED})
    mark_as_advanced(CCRYPTO_INCLUDE_DIR)
    tr_fixup_list_option(WITH_CRYPTO "ccrypto" CCRYPTO_INCLUDE_DIR "AUTO" CCRYPTO_IS_REQUIRED)
    if(WITH_CRYPTO STREQUAL "ccrypto")
        set(CRYPTO_PKG "ccrypto")
        set(CRYPTO_INCLUDE_DIRS)
        set(CRYPTO_LIBRARIES)
    endif()
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
        gtkmm-3.0>=${GTK_MINIMUM}
        glibmm-2.4>=${GLIB_MINIMUM}
        giomm-2.4>=${GIO_MINIMUM})

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

    set(QT_REQUIRED_MODULES Core Gui Widgets Network LinguistTools)
    set(QT_OPTIONAL_MODULES DBus AxContainer AxServer)
    set(MISSING_QT_MODULE)

    set(Qt_NAMES Qt6 Qt5)
    if(NOT USE_QT_VERSION STREQUAL "AUTO")
        set(Qt_NAMES Qt${USE_QT_VERSION})
    endif()

    find_package(Qt NAMES ${Qt_NAMES} ${QT_MINIMUM} QUIET)
    if(Qt_FOUND)
        if(WIN32 AND Qt_VERSION_MAJOR EQUAL 5)
            list(APPEND QT_REQUIRED_MODULES WinExtras)
        endif()

        foreach(M ${QT_REQUIRED_MODULES})
            find_package(Qt${Qt_VERSION_MAJOR}${M} ${QT_MINIMUM} QUIET)
            if(Qt${Qt_VERSION_MAJOR}${M}_FOUND)
                if(NOT M STREQUAL "LinguistTools")
                    list(APPEND QT_TARGETS Qt${Qt_VERSION_MAJOR}::${M})
                endif()
            else()
                set(QT_TARGETS)
                set(MISSING_QT_MODULE "${M}")
                break()
            endif()
        endforeach()
    endif()

    if(QT_TARGETS)
        foreach(M ${QT_OPTIONAL_MODULES})
            find_package(Qt${Qt_VERSION_MAJOR}${M} ${QT_MINIMUM} QUIET)
            if(Qt${Qt_VERSION_MAJOR}${M}_FOUND)
                list(APPEND QT_TARGETS Qt${Qt_VERSION_MAJOR}::${M})
            endif()
        endforeach()

        if(Qt${Qt_VERSION_MAJOR}AxContainer_FOUND AND Qt${Qt_VERSION_MAJOR}AxServer_FOUND)
            set(ENABLE_QT_COM_INTEROP ON)

            find_program(MIDL_EXECUTABLE midl)
            if(NOT MIDL_EXECUTABLE)
                set(ENABLE_QT_COM_INTEROP OFF)
            endif()
        endif()

        if(Qt${Qt_VERSION_MAJOR}DBus_FOUND)
            set(ENABLE_QT_DBUS_INTEROP ON)
        endif()
    endif()

    set(QT_FOUND ON)
    if(NOT QT_TARGETS OR NOT (ENABLE_QT_COM_INTEROP OR ENABLE_QT_DBUS_INTEROP))
        if(QT_IS_REQUIRED)
            message(FATAL_ERROR "Unable to find required Qt libraries (Qt${Qt_VERSION_MAJOR}${MISSING_QT_MODULE})")
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

set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third-party)

tr_add_external_auto_library(DEFLATE libdeflate deflate)
if(NOT USE_SYSTEM_DEFLATE)
    set(DEFLATE_VERSION 1.10)
endif()

tr_add_external_auto_library(EVENT2 libevent event
    CMAKE_ARGS
        -DEVENT__DISABLE_OPENSSL:BOOL=ON
        -DEVENT__DISABLE_BENCHMARK:BOOL=ON
        -DEVENT__DISABLE_TESTS:BOOL=ON
        -DEVENT__DISABLE_REGRESS:BOOL=ON
        -DEVENT__DISABLE_SAMPLES:BOOL=ON
        -DEVENT__LIBRARY_TYPE:STRING=STATIC)

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

tr_add_external_auto_library(PSL libpsl psl)

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

add_subdirectory(third-party/arc4)

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

## Compiler standard version

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

### Compiler Warnings

set(C_WARNING_FLAGS)
set(CXX_WARNING_FLAGS)

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

if(MSVC)
    set(WARNING_CANDIDATES
        /W4)

    foreach(FLAG ${WARNING_CANDIDATES})
        list(APPEND C_WARNING_FLAGS ${FLAG})
        list(APPEND CXX_WARNING_FLAGS ${FLAG})
    endforeach()
else()
    set(WARNING_CANDIDATES
        -W
        -Wall
        -Wextra
        -Wcast-align
        -Wduplicated-cond
        -Wexit-time-destructors
        -Wextra-semi
        -Wextra-semi-stmt
        -Wextra-tokens
        -Wfloat-equal
        -Wgnu
        -Winit-self
        -Wint-in-bool-context
        -Wlogical-op
        -Wmissing-format-attribute
        -Wnull-dereference
        -Wpointer-arith
        -Wredundant-decls
        -Wredundant-move
        -Wreorder-ctor
        -Wrestrict
        -Wreturn-std-move
        -Wself-assign
        -Wself-move
        -Wsemicolon-before-method-body
        -Wsentinel
        -Wshadow
        -Wsign-compare
        -Wsometimes-uninitialized
        -Wstring-conversion
        -Wsuggest-destructor-override
        -Wsuggest-override
        -Wuninitialized
        -Wunreachable-code
        -Wunused
        -Wunused-const-variable
        -Wunused-parameter
        -Wunused-result
        -Wwrite-strings)

    if(MINGW)
        # Disable excessive warnings since we're using __USE_MINGW_ANSI_STDIO
        # Hopefully, any potential issues will be spotted on other platforms
        list(APPEND WARNING_CANDIDATES -Wno-format)
    else()
        list(APPEND WARNING_CANDIDATES -Wformat-security)
    endif()

    set(CMAKE_REQUIRED_FLAGS)

    foreach(FLAG -Werror /WX)
        tr_make_id("${FLAG}" FLAG_ID)
        set(CACHE_ID "${CMAKE_C_COMPILER_ID}_C_HAS${FLAG_ID}")
        string(TOLOWER "${CACHE_ID}" CACHE_ID)
        check_c_compiler_flag(${FLAG} ${CACHE_ID})
        if (${CACHE_ID})
            # Make sure the next loop only adds flags that are relevant for a particular language
            set(CMAKE_REQUIRED_FLAGS ${FLAG})
            break()
        endif()
    endforeach()

    foreach(FLAG ${WARNING_CANDIDATES})
        tr_make_id("${FLAG}" FLAG_ID)

        # if available, add to C warnings
        set(CACHE_ID "${CMAKE_C_COMPILER_ID}_C_HAS${FLAG_ID}")
        string(TOLOWER "${CACHE_ID}" CACHE_ID)
        check_c_compiler_flag(${FLAG} ${CACHE_ID})
        if (${CACHE_ID})
            list(APPEND C_WARNING_FLAGS ${FLAG})
        endif()

        # if available, add to CXX warnings
        set(CACHE_ID "${CMAKE_CXX_COMPILER_ID}_CXX_HAS${FLAG_ID}")
        string(TOLOWER "${CACHE_ID}" CACHE_ID)
        check_cxx_compiler_flag(${FLAG} ${CACHE_ID})
        if (${CACHE_ID})
            list(APPEND CXX_WARNING_FLAGS ${FLAG})
        endif()

        unset(CACHE_ID)
        unset(FLAG_ID)
    endforeach()

    unset(CMAKE_REQUIRED_FLAGS)
endif()

###

include(LargeFileSupport)

set(NEEDED_HEADERS
    sys/statvfs.h
    xfs/xfs.h
    xlocale.h)

if(ENABLE_NLS)
    check_library_exists(intl libintl_gettext "" HAVE_LIBINTL)
    if(HAVE_LIBINTL)
        set(LIBINTL_LIBRARY intl)
        # check_function_exists() below may need this
        # when looking for gettext() and ngettext()
        list(APPEND CMAKE_REQUIRED_LIBRARIES intl)
    endif()

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
    copy_file_range
    copyfile
    daemon
    fallocate64
    flock
    getmntent
    gettext
    htonll
    mkdtemp
    ngettext
    ntohll
    posix_fadvise
    posix_fallocate
    pread
    pwrite
    sendfile64
    statvfs
    strlcpy
    syslog)

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

if(RUN_CLANG_TIDY STREQUAL "AUTO")
    if (DEFINED ENV{LGTM_SRC} OR DEFINED ENV{APPVEYOR}) # skip clang-tidy on LGTM/appveyor
        set(RUN_CLANG_TIDY OFF)
    else()
        set(RUN_CLANG_TIDY ON)
    endif()
endif()

if(RUN_CLANG_TIDY)
    message(STATUS "Looking for clang-tidy")
    find_program(CLANG_TIDY clang-tidy)
    if (CLANG_TIDY STREQUAL "CLANG_TIDY-NOTFOUND")
        message(STATUS "Looking for clang-tidy - not found")
    else()
        message(STATUS "Looking for clang-tidy - found")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY}")
    endif()
endif()

if(ENABLE_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

function(tr_install_web DST_DIR)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/web/public_html DESTINATION ${DST_DIR})
endfunction()

add_subdirectory(libtransmission)

set(MAC_PROJECT_DIR macosx)

if(ENABLE_GTK AND ENABLE_NLS)
    find_package(Gettext 0.19.7 REQUIRED)
    add_subdirectory(po)
endif()

foreach(P cli daemon gtk mac qt utils web)
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
    install(FILES AUTHORS COPYING README.md extras/send-email-when-torrent-done.sh DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY news docs DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

if(MSVC AND ENABLE_DAEMON AND ENABLE_QT AND ENABLE_UTILS AND WITH_CRYPTO STREQUAL "openssl")
    add_subdirectory(dist/msi)
endif()

set(CPACK_SOURCE_GENERATOR TXZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${TR_NAME}-${TR_USER_AGENT_PREFIX}")
if(NOT TR_STABLE_RELEASE)
    string(APPEND CPACK_SOURCE_PACKAGE_FILE_NAME "-r${TR_VCS_REVISION}")
endif()
list(APPEND CPACK_SOURCE_IGNORE_FILES
    "${CMAKE_BINARY_DIR}"
    "[.]git"
    "node_modules"
)

## Code Formatting

if(GIT_FOUND)
    execute_process(COMMAND
        "${GIT_EXECUTABLE}" rev-parse --show-toplevel
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE TR_GIT_ROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(TR_GIT_ROOT AND IS_DIRECTORY "${TR_GIT_ROOT}/.git")
        configure_file(
            "${CMAKE_SOURCE_DIR}/extras/pre-commit"
            "${TR_GIT_ROOT}/.git/hooks/pre-commit"
            COPYONLY
        )
        add_custom_target(check-format
            COMMAND "${CMAKE_SOURCE_DIR}/code_style.sh" --check
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
        add_custom_target(format
            COMMAND "${CMAKE_SOURCE_DIR}/code_style.sh"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    endif()
    unset(TR_GIT_ROOT)
endif()

include(CPack)
