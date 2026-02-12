if(ENABLE_NLS)
    find_package(Intl)
endif()

add_library(trbase STATIC)

target_sources(trbase
    PRIVATE
        env.cc
        env.h
        error-types.h
        error.cc
        error.h
        file-posix.cc
        file-utils.cc
        file-utils.h
        file-win32.cc
        file.cc
        file.h
        i18n.h
        string-utils.cc
        string-utils.h
        $<$<BOOL:${IS_APPLE_CLANG}>:string-utils.mm>
        tr-assert.cc
        tr-assert.h
        tr-strbuf.h
        values.h
        tr-macros.h)

tr_allow_compile_if(
    [=[[WIN32]]=]
        file-win32.cc
    [=[[NOT WIN32]]=]
        file-posix.cc)

target_compile_definitions(trbase
    PUBLIC
        $<$<BOOL:${Intl_FOUND}>:ENABLE_GETTEXT>)

tr_target_compile_definitions_for_functions(trbase
    PRIVATE
        copyfile
        copy_file_range
        fallocate64
        flock
        htonll
        mkdtemp
        ntohll
        posix_fadvise
        posix_fallocate
        pread
        pwrite
        sendfile64)

target_include_directories(trbase
    PUBLIC
        ${CMAKE_SOURCE_DIR})

target_link_libraries(trbase
    PUBLIC
        transmission::fmt-header-only
        utf8::cpp
        wildmat)

set_property(
    TARGET trbase
    PROPERTY FOLDER "lib")
