AM_CPPFLAGS = -I$(top_srcdir) $(WX_CPPFLAGS) $(LIBEVENT_CPPFLAGS)
AM_CFLAGS = $(PTHREAD_CFLAGS) $(OPENSSL_CFLAGS)
AM_CXXFLAGS = $(WX_CXXFLAGS)

SUBDIRS = images

bin_PROGRAMS = Xmission

Xmission_SOURCES = \
  speed-stats.cc \
  filter.cc \
  torrent-list.cc \
  torrent-stats.cc \
  xmission.cc

Xmission_LDADD = \
  $(top_builddir)/libtransmission/libtransmission.a \
  $(top_builddir)/third-party/libevent/libevent.la \
  $(top_builddir)/third-party/miniupnp/libminiupnp.a \
  $(WX_LIBS) $(OPENSSL_LIBS) $(PTHREAD_LIBS) -lm
