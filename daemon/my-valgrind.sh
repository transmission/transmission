#!/bin/sh
export G_SLICE=always-malloc
export G_DEBUG=gc-friendly
export GLIBCXX_FORCE_NEW=1
#valgrind --tool=memcheck --leak-check=full --leak-resolution=high --num-callers=64 --log-file=x-valgrind --show-reachable=yes ./transmission-daemon -f
valgrind --tool=cachegrind ./transmission-daemon -f -g /home/charles/.config/transmission
