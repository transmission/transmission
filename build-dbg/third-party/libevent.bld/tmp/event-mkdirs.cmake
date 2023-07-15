# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "X:/transmission-jr/third-party/libevent"
  "X:/transmission-jr/build-dbg/third-party/libevent.bld/src/event-build"
  "X:/transmission-jr/build-dbg/third-party/libevent.bld/pfx"
  "X:/transmission-jr/build-dbg/third-party/libevent.bld/tmp"
  "X:/transmission-jr/build-dbg/third-party/libevent.bld/src/event-stamp"
  "X:/transmission-jr/build-dbg/third-party/libevent.bld/src"
  "X:/transmission-jr/build-dbg/third-party/libevent.bld/src/event-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "X:/transmission-jr/build-dbg/third-party/libevent.bld/src/event-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "X:/transmission-jr/build-dbg/third-party/libevent.bld/src/event-stamp${cfgdir}") # cfgdir has leading slash
endif()
