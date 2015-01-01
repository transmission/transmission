if BUILD_DHT
  DHT_DIR = dht
endif
if BUILD_B64
  B64_DIR = libb64
endif
if BUILD_UTP
  UTP_DIR = libutp
endif
if BUILD_MINIUPNP
  MINIUPNP_DIR = miniupnp
endif
if BUILD_NATPMP 
  NATPMP_DIR = libnatpmp 
endif 

SUBDIRS = \
  $(DHT_DIR) \
  $(B64_DIR) \
  $(NATPMP_DIR) \
  $(MINIUPNP_DIR) \
  $(UTP_DIR)

EXTRA_DIST = \
  macosx-libevent-config.h \
  macosx-libevent-event-config.h
