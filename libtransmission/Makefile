include ../Makefile.config
include ../Makefile.common

SRCS = transmission.c bencode.c net.c tracker.c peer.c inout.c \
       metainfo.c sha1.c utils.c fdlimit.c clients.c completion.c \
       platform.c ratecontrol.c choking.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS += -D__TRANSMISSION__

libtransmission.a: $(OBJS)
	@echo "Library $@"
	@ar ru $@ $(OBJS)
	@ranlib $@

%.o: %.c ../Makefile.config ../Makefile.common Makefile
	$(CC_RULE)

clean:
	@echo "Clean libtransmission.a"
	@echo "Clean $(OBJS)"
	@$(RM) libtransmission.a $(OBJS)

.depend: $(SRCS) ../Makefile.config ../Makefile.common Makefile
	$(DEP_RULE)

-include .depend
