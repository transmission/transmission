AM_CPPFLAGS = \
  -I$(top_srcdir) \
  $(WX_CPPFLAGS) \
  $(LIBEVENT_CPPFLAGS)
AM_CFLAGS = $(PTHREAD_CFLAGS)
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
  $(WX_LIBS) \
  $(PTHREAD_LIBS) -lm
