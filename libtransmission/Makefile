include ../Makefile.config
include ../Makefile.common

SRCS = transmission.c bencode.c net.c tracker.c peer.c inout.c \
       metainfo.c sha1.c utils.c fdlimit.c clients.c completion.c \
       platform.c ratecontrol.c choking.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS += -D__TRANSMISSION__

libtransmission.a: $(OBJS)
ifeq ($(SYSTEM),Darwin)
	libtool -static $(OBJS) -o $@
else
	ar ru $@ $(OBJS)
	ranlib $@
endif

%.o: %.c ../Makefile.config ../Makefile.common Makefile
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) libtransmission.a $(OBJS)

.depend: $(SRCS) Makefile
	$(RM) .depend
	$(foreach SRC, $(SRCS), $(CC) -MM $(SRC) -D__TRANSMISSION__ >> .depend;)

include .depend
