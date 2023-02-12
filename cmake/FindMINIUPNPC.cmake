if(MINIUPNPC_PREFER_STATIC_LIB)
    set(MINIUPNPC_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

if(UNIX)
  find_package(PkgConfig QUIET)
  pkg_check_modules(_MINIUPNPC QUIET libminiupnpc)
endif()

find_path(MINIUPNPC_INCLUDE_DIR NAMES miniupnpc/miniupnpc.h HINTS ${_MINIUPNPC_INCLUDEDIR})
find_library(MINIUPNPC_LIBRARY NAMES miniupnpc libminiupnpc HINTS ${_MINIUPNPC_LIBDIR})

if(MINIUPNPC_INCLUDE_DIR)
    if(_MINIUPNPC_VERSION)
        set(MINIUPNPC_VERSION ${_MINIUPNPC_VERSION})
    else()
        file(STRINGS "${MINIUPNPC_INCLUDE_DIR}/miniupnpc/miniupnpc.h" MINIUPNPC_VERSION_STR REGEX "^#define[\t ]+MINIUPNPC_VERSION[\t ]+\"[^\"]+\"")
        if(MINIUPNPC_VERSION_STR MATCHES "\"([^\"]+)\"")
            set(MINIUPNPC_VERSION "${CMAKE_MATCH_1}")
        endif()

        # Let's hope it's 1.7 or higher, since it provides
        # MINIUPNPC_API_VERSION and we won't have to figure
        # it out on our own
        file(STRINGS "${MINIUPNPC_INCLUDE_DIR}/miniupnpc/miniupnpc.h" MINIUPNPC_API_VERSION_STR REGEX "^#define[\t ]+MINIUPNPC_API_VERSION[\t ]+[0-9]+")
        if(MINIUPNPC_API_VERSION_STR MATCHES "^#define[\t ]+MINIUPNPC_API_VERSION[\t ]+([0-9]+)")
            set(MINIUPNPC_API_VERSION "${CMAKE_MATCH_1}")
        endif()
    endif()

    if(MINIUPNPC_LIBRARY)
        # Or maybe it's miniupnp 1.6
        if(NOT DEFINED MINIUPNPC_API_VERSION)
            file(WRITE ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/CheckMiniUPnPC_1.6.c
                "
                #include <stdlib.h>
                #include <errno.h>
                #include <miniupnpc/miniupnpc.h>
                #include <miniupnpc/upnpcommands.h>
                int main()
                {
                    struct UPNPDev * devlist;
                    struct UPNPUrls urls;
                    struct IGDdatas data;
                    char lanaddr[16];
                    char portStr[8];
                    char intPort[8];
                    char intClient[16];
                    upnpDiscover( 2000, NULL, NULL, 0, 0, &errno );
                    UPNP_GetValidIGD( devlist, &urls, &data, lanaddr, sizeof( lanaddr ) ); 
                    UPNP_GetSpecificPortMappingEntry( urls.controlURL, data.first.servicetype,
                                        portStr, \"TCP\", intClient, intPort, NULL, NULL, NULL );
                    return 0;
                }
                ")
            try_compile(_MINIUPNPC_HAVE_VERSION_1_6
                ${CMAKE_BINARY_DIR}
                ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/CheckMiniUPnPC_1.6.c
                COMPILE_DEFINITIONS -DINCLUDE_DIRECTORIES=${MINIUPNPC_INCLUDE_DIR}
                LINK_LIBRARIES ${MINIUPNPC_LIBRARY}
                OUTPUT_VARIABLE OUTPUT)
            if(_MINIUPNPC_HAVE_VERSION_1_6)
                if(NOT DEFINED MINIUPNPC_VERSION)
                    set(MINIUPNPC_VERSION 1.6)
                endif()
                set(MINIUPNPC_API_VERSION 8)
            endif()
        endif()

        # Or maybe it's miniupnp 1.5
        if(NOT DEFINED MINIUPNPC_API_VERSION)
            file(WRITE ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/CheckMiniUPnPC_1.5.c
                "
                #include <stdlib.h>
                #include <miniupnpc/miniupnpc.h>
                #include <miniupnpc/upnpcommands.h>
                int main()
                {
                    struct UPNPDev * devlist;
                    struct UPNPUrls urls;
                    struct IGDdatas data;
                    char lanaddr[16];
                    char portStr[8];
                    char intPort[8];
                    char intClient[16];
                    upnpDiscover( 2000, NULL, NULL, 0 );
                    UPNP_GetValidIGD( devlist, &urls, &data, lanaddr, sizeof( lanaddr ) ); 
                    UPNP_GetSpecificPortMappingEntry( urls.controlURL, data.first.servicetype,
                                        portStr, \"TCP\", intClient, intPort );
                    return 0;
                }
                ")
            try_compile(_MINIUPNPC_HAVE_VERSION_1_5
                ${CMAKE_BINARY_DIR}
                ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/CheckMiniUPnPC_1.5.c
                COMPILE_DEFINITIONS -DINCLUDE_DIRECTORIES=${MlINIUPNPC_INCLUDE_DIR}
                LINK_LIBRARIES ${MINIUPNPC_LIBRARY}
                OUTPUT_VARIABLE OUTPUT)
            if(_MINIUPNPC_HAVE_VERSION_1_5)
                if(NOT DEFINED MINIUPNPC_VERSION)
                    set(MINIUPNPC_VERSION 1.5)
                endif()
                set(MINIUPNPC_API_VERSION 5)
            endif()
        endif()
    endif()
endif()

set(MINIUPNPC_INCLUDE_DIRS ${MINIUPNPC_INCLUDE_DIR})
set(MINIUPNPC_LIBRARIES ${MINIUPNPC_LIBRARY})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(MINIUPNPC
    REQUIRED_VARS
        MINIUPNPC_LIBRARY
        MINIUPNPC_INCLUDE_DIR
        MINIUPNPC_API_VERSION
    VERSION_VAR
        MINIUPNPC_VERSION
)

mark_as_advanced(MINIUPNPC_INCLUDE_DIR MINIUPNPC_LIBRARY)

if(MINIUPNPC_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${MINIUPNPC_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(MINIUPNPC_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
