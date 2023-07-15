# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "X:/transmission-jr/third-party/miniupnpc"
  "X:/transmission-jr/build-dbg/third-party/miniupnpc.bld/src/miniupnpc-build"
  "X:/transmission-jr/build-dbg/third-party/miniupnpc.bld/pfx"
  "X:/transmission-jr/build-dbg/third-party/miniupnpc.bld/tmp"
  "X:/transmission-jr/build-dbg/third-party/miniupnpc.bld/src/miniupnpc-stamp"
  "X:/transmission-jr/build-dbg/third-party/miniupnpc.bld/src"
  "X:/transmission-jr/build-dbg/third-party/miniupnpc.bld/src/miniupnpc-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "X:/transmission-jr/build-dbg/third-party/miniupnpc.bld/src/miniupnpc-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "X:/transmission-jr/build-dbg/third-party/miniupnpc.bld/src/miniupnpc-stamp${cfgdir}") # cfgdir has leading slash
endif()
