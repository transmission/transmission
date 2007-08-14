AM_CPPFLAGS = -I$(top_srcdir) $(LIBEVENT_CPPFLAGS)
AM_CFLAGS = $(PTHREAD_CFLAGS)

bin_PROGRAMS = transmission-cli

transmission_cli_SOURCES = transmissioncli.c

transmission_cli_LDADD = \
    ../libtransmission/libtransmission.a \
    $(LIBEVENT_LIBS) \
    $(PTHREAD_LIBS) -lm
