include ../Makefile.config
include ../Makefile.common

SRCS = conf.c dialogs.c main.c trcellrenderertorrent.c util.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS  += $(CFLAGS_GTK) -I../libtransmission
LDFLAGS += $(LDFLAGS_GTK)

transmission-gtk: $(OBJS)
	$(CC) -o $@ $(OBJS) ../libtransmission/libtransmission.a $(LDFLAGS)

%.o: %.c ../Makefile.config ../Makefile.common Makefile
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) transmission-gtk $(OBJS)

.depend: $(SRCS) Makefile
	$(RM) .depend
	$(foreach SRC, $(SRCS), $(CC) -MM $(SRC) -D__TRANSMISSION__ >> .depend;)

include .depend
