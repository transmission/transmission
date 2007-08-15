#!/bin/sh

LIBTOOLIZE=libtoolize
if [ "$(uname)" == "Darwin" ] ; then
  LIBTOOLIZE=glibtoolize
fi

autoreconf -fiv -I config -I m4
