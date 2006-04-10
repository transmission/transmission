include ../Makefile.config
include ../Makefile.common

SRCS = conf.c dialogs.c main.c trcellrenderertorrent.c util.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS  += $(CFLAGS_GTK) -I../libtransmission
LDFLAGS += $(LDFLAGS_GTK) ../libtransmission/libtransmission.a

transmission-gtk: $(OBJS) ../libtransmission/libtransmission.a
	$(LINK_RULE)

%.o: %.c ../Makefile.config ../Makefile.common Makefile
	$(CC_RULE)

clean:
	@echo "Clean transmission-gtk"
	@$(RM) transmission-gtk
	@echo "Clean $(OBJS)"
	@$(RM) $(OBJS)

.depend: $(SRCS) ../Makefile.config ../Makefile.common Makefile
	$(DEP_RULE)

-include .depend
