AM_CPPFLAGS = -I@top_srcdir@ 
AM_CFLAGS = $(PTHREAD_CFLAGS)


bin_PROGRAMS = transmission-cli

transmission_cli_SOURCES = transmissioncli.c
transmission_cli_LDADD = ../libtransmission/libtransmission.a $(PTHREAD_LIBS) -lm
