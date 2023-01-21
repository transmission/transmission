# Based on AC_SYS_LARGEFILE

if(NOT DEFINED NO_LFS_MACROS_REQUIRED)
    include(CheckCSourceCompiles)

    # Check that off_t can represent 2**63 - 1 correctly.
    # We can't simply define LARGE_OFF_T to be 9223372036854775807,
    # since some C++ compilers masquerading as C compilers
    # incorrectly reject 9223372036854775807.
    #
    # Begin with a linefeed to ensure the first line isn't part
    # of the #defines below
    set(LFS_TEST_PROGRAM "
        #include <sys/types.h>
        #define LARGE_OFF_T (((off_t) 1 << 62) - 1 + ((off_t) 1 << 62))
        int off_t_is_large[(LARGE_OFF_T % 2147483629 == 721 && LARGE_OFF_T % 2147483647 == 1) ? 1 : -1];
        int main() { return 0; }")

    check_c_source_compiles("${LFS_TEST_PROGRAM}" NO_LFS_MACROS_REQUIRED)
    if(NOT NO_LFS_MACROS_REQUIRED)
        if(NOT DEFINED FILE_OFFSET_BITS_LFS_MACRO_REQUIRED)
            check_c_source_compiles("#define _FILE_OFFSET_BITS 64 ${LFS_TEST_PROGRAM}" FILE_OFFSET_BITS_LFS_MACRO_REQUIRED)
            if(NOT FILE_OFFSET_BITS_LFS_MACRO_REQUIRED AND NOT DEFINED LARGE_FILES_LFS_MACRO_REQUIRED)
                check_c_source_compiles("#define _LARGE_FILES 1 ${LFS_TEST_PROGRAM}" LARGE_FILES_LFS_MACRO_REQUIRED)
            endif()
        endif()
    endif()

    unset(LFS_TEST_PROGRAM)
endif()

if(FILE_OFFSET_BITS_LFS_MACRO_REQUIRED)
    add_definitions(-D_FILE_OFFSET_BITS=64)
endif()

if(LARGE_FILES_LFS_MACRO_REQUIRED)
    add_definitions(-D_LARGE_FILES=1)
endif()
