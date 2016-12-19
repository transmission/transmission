if(UNIX)
    find_package(PkgConfig QUIET)
    pkg_check_modules(PC_SYSTEMD QUIET libsystemd)
endif()

find_path(SYSTEMD_INCLUDE_DIR NAMES systemd/sd-daemon.h HINTS ${PC_SYSTEMD_INCLUDE_DIRS})
find_library(SYSTEMD_LIBRARY NAMES systemd HINTS ${PC_SYSTEMD_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(SYSTEMD
    REQUIRED_VARS
        SYSTEMD_LIBRARY
        SYSTEMD_INCLUDE_DIR
)

mark_as_advanced(SYSTEMD_INCLUDE_DIR SYSTEMD_LIBRARY)

set(SYSTEMD_INCLUDE_DIRS ${SYSTEMD_INCLUDE_DIR})
set(SYSTEMD_LIBRARIES ${SYSTEMD_LIBRARY})
