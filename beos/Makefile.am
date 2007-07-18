AM_CPPFLAGS = -I@top_srcdir@ -I../libtransmission -IlibPrefs
AM_CFLAGS = $(PTHREAD_CFLAGS)

noinst_HEADERS = \
    TRApplication.h \
    TRInfoWindow.h \
    TRPrefsWindow.h \
    TRTransfer.h

bin_PROGRAMS = Transmission

Transmission_SOURCES = \
    TRApplication.cpp \
    TRInfoWindow.cpp \
    TRPrefsWindow.cpp \
    TRTransfer.cpp \
    TRWindow.cpp

Transmission_LDADD = \
    libPrefs/libPrefs.a -lbe -ltracker \
    ../libtransmission/libtransmission.a $(PTHREAD_LIBS) -lm
