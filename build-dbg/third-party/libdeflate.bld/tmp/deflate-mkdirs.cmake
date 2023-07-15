# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "X:/transmission-jr/third-party/libdeflate"
  "X:/transmission-jr/build-dbg/third-party/libdeflate.bld/src/deflate-build"
  "X:/transmission-jr/build-dbg/third-party/libdeflate.bld/pfx"
  "X:/transmission-jr/build-dbg/third-party/libdeflate.bld/tmp"
  "X:/transmission-jr/build-dbg/third-party/libdeflate.bld/src/deflate-stamp"
  "X:/transmission-jr/build-dbg/third-party/libdeflate.bld/src"
  "X:/transmission-jr/build-dbg/third-party/libdeflate.bld/src/deflate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "X:/transmission-jr/build-dbg/third-party/libdeflate.bld/src/deflate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "X:/transmission-jr/build-dbg/third-party/libdeflate.bld/src/deflate-stamp${cfgdir}") # cfgdir has leading slash
endif()
