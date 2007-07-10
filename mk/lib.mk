# $Id$

include ../mk/config.mk
include ../mk/common.mk

SRCS = bencode.c choking.c clients.c completion.c fastresume.c fdlimit.c \
       http.c inout.c ipcparse.c list.c makemeta.c metainfo.c natpmp.c \
       net.c peer.c platform.c ratecontrol.c sha1.c shared.c strlcat.c \
       strlcpy.c torrent.c tracker.c transmission.c upnp.c utils.c xml.c \
       basename.c dirname.c

OBJS = $(SRCS:%.c=%.o)

CFLAGS += -D__TRANSMISSION__

libtransmission.a: $(OBJS)
	@echo "Library $@"
	@ar ru $@ $(OBJS)
	@ranlib $@

%.o: %.c ../mk/config.mk ../mk/common.mk ../mk/cli.mk
	$(CC_RULE)

clean:
	@echo "Clean libtransmission.a"
	@echo "Clean $(OBJS)"
	@$(RM) libtransmission.a $(OBJS)

.depend: $(SRCS) ../mk/config.mk ../mk/common.mk ../mk/cli.mk
	$(DEP_RULE)

-include .depend
