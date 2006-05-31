# $Id$

include ../mk/config.mk
include ../mk/common.mk

SRCS = transmission.c bencode.c net.c tracker.c peer.c inout.c \
       metainfo.c sha1.c utils.c fdlimit.c clients.c completion.c \
       platform.c ratecontrol.c choking.c
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
