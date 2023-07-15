# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "X:/transmission-jr/third-party/libnatpmp"
  "X:/transmission-jr/build-dbg/third-party/libnatpmp.bld/src/natpmp-build"
  "X:/transmission-jr/build-dbg/third-party/libnatpmp.bld/pfx"
  "X:/transmission-jr/build-dbg/third-party/libnatpmp.bld/tmp"
  "X:/transmission-jr/build-dbg/third-party/libnatpmp.bld/src/natpmp-stamp"
  "X:/transmission-jr/build-dbg/third-party/libnatpmp.bld/src"
  "X:/transmission-jr/build-dbg/third-party/libnatpmp.bld/src/natpmp-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "X:/transmission-jr/build-dbg/third-party/libnatpmp.bld/src/natpmp-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "X:/transmission-jr/build-dbg/third-party/libnatpmp.bld/src/natpmp-stamp${cfgdir}") # cfgdir has leading slash
endif()
