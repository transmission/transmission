#!/bin/sh
autoreconf --force --install -I config -I m4
cd third-party/libevent
sh ./autogen.sh
