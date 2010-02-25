noinst_LIBRARIES = libnatpmp.a

AM_CFLAGS = @PTHREAD_CFLAGS@ -DENABLE_STRNATPMPERR

libnatpmp_a_SOURCES = \
    getgateway.c \
    natpmp.c \
    wingettimeofday.c

noinst_HEADERS = \
    declspec.h \
    getgateway.h \
    natpmp.h \
    wingettimeofday.h

EXTRA_DIST = \
    README \
    LICENSE
