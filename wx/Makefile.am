AM_CPPFLAGS = -I@top_srcdir@ @WX_CPPFLAGS@
AM_CXXFLAGS = @WX_CXXFLAGS@
SUBDIRS = images

bin_PROGRAMS = Xmission

Xmission_SOURCES = \
    speed-stats.cc \
    torrent-filter.cc \
    torrent-list.cc \
    torrent-stats.cc \
    xmission.cc

Xmission_LDADD = ../libtransmission/libtransmission.a @WX_LIBS@ $(PTHREAD_LIBS) -lm
