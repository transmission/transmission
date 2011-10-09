if BUILD_UTP
  UTP_DIR = libutp
endif
if BUILD_MINIUPNP
  MINIUPNP_DIR = miniupnp
endif

SUBDIRS = \
  dht \
  libnatpmp \
  $(MINIUPNP_DIR) \
  $(UTP_DIR)

EXTRA_DIST = \
  macosx-libevent-config.h \
  macosx-libevent-event-config.h

install:

uninstall:
