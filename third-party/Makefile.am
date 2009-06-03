
if BUILD_LIBEVENT
  LIBEVENT_DIR = libevent
else
  LIBEVENT_DIR =
endif

SUBDIRS = libnatpmp miniupnp dht $(LIBEVENT_DIR)

EXTRA_DIST = \
    libevent \
    macosx-libevent-config.h

install:
