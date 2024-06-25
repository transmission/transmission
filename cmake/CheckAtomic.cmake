# - Try to find if 64-bits atomics need -latomic linking
# Once done this will define
#  HAVE_CXX_ATOMICS_WITHOUT_LIB - Whether atomic types work without -latomic

include(CheckCXXSourceCompiles)
include(CheckLibraryExists)

# Sometimes linking against libatomic is required for atomic ops, if
# the platform doesn't support lock-free atomics.

function(check_working_cxx_atomics VARNAME)
    check_cxx_source_compiles("
        #include <atomic>
        int main() {
            std::atomic<long long> x;
            return std::atomic_is_lock_free(&x);
        }
    " ${VARNAME})
endfunction()

# Check for atomic operations.
if(MSVC)
    # This isn't necessary on MSVC.
    set(HAVE_CXX_ATOMICS_WITHOUT_LIB TRUE)
else()
    # First check if atomics work without the library.
    check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITHOUT_LIB)
endif()

# If not, check if the library exists, and atomics work with it.
if(NOT HAVE_CXX_ATOMICS_WITHOUT_LIB)
    check_library_exists(atomic __atomic_load_8 "" HAVE_LIBATOMIC)
    if(NOT HAVE_LIBATOMIC)
        message(STATUS "Host compiler appears to require libatomic, but cannot locate it.")
    endif()

    list(APPEND CMAKE_REQUIRED_LIBRARIES "atomic")
    check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITH_LIB)
    list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES "atomic")
    if(NOT HAVE_CXX_ATOMICS_WITH_LIB)
        message(FATAL_ERROR "Host compiler must support std::atomic!")
    endif()
endif()
