# Create an INTERFACE IMPORTED target for libmaxminddb
add_library(libmaxminddb::maxminddb INTERFACE IMPORTED)

# Set the include directories
target_include_directories(libmaxminddb::maxminddb
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/libmaxminddb/include)

# Set the include directories
target_include_directories(libmaxminddb::maxminddb
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../build/third-party/libmaxminddb/generated)

target_link_directories(libmaxminddb::maxminddb
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../build/third-party/libmaxminddb)  # For example, /usr/local/lib or /usr/lib

# Link libmaxminddb to the Transmission executable or library
target_link_libraries(libmaxminddb::maxminddb
    INTERFACE
        libmaxminddb.a)