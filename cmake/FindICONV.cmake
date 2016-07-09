# Grabbed from http://public.kitware.com/Bug/view.php?id=13517 and slightly modified.

find_path(ICONV_INCLUDE_DIR iconv.h)
find_library(ICONV_LIBRARY NAMES iconv libiconv libiconv-2 c)

set(ICONV_INCLUDE_DIRS ${ICONV_INCLUDE_DIR})
set(ICONV_LIBRARIES ${ICONV_LIBRARY})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(ICONV
    REQUIRED_VARS
        ICONV_LIBRARY
        ICONV_INCLUDE_DIR
    VERSION_VAR
        ICONV_VERSION
)

if(ICONV_FOUND AND NOT DEFINED ICONV_SECOND_ARGUMENT_IS_CONST)
    include(CheckCSourceCompiles)

    set(CMAKE_REQUIRED_INCLUDES ${ICONV_INCLUDE_DIRS})
    set(CMAKE_REQUIRED_LIBRARIES ${ICONV_LIBRARIES})

    check_c_source_compiles("
        #include <iconv.h>
        int main ()
        {
            iconv_t conv = 0;
            const char * in = 0;
            size_t ilen = 0;
            char * out = 0;
            size_t olen = 0;
            iconv (conv, &in, &ilen, &out, &olen);
            return 0;
        }"
        ICONV_SECOND_ARGUMENT_IS_CONST
        FAIL_REGEX "incompatible pointer type"
        FAIL_REGEX "discards qualifiers in nested pointer types")

    set(CMAKE_REQUIRED_INCLUDES)
    set(CMAKE_REQUIRED_LIBRARIES)
endif()

mark_as_advanced(ICONV_INCLUDE_DIR ICONV_LIBRARY ICONV_SECOND_ARGUMENT_IS_CONST)
