AM_CPPFLAGS = -I@top_srcdir@ @WX_CPPFLAGS@
AM_CXXFLAGS = @WX_CXXFLAGS@
SUBDIRS = images

bin_PROGRAMS = Xmission

Xmission_SOURCES = xmission.cc torrent-list.cc

Xmission_LDADD = ../libtransmission/libtransmission.a @WX_LIBS@ $(PTHREAD_LIBS) -lm
