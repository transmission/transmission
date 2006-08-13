# $Id$

include ../mk/config.mk
include ../mk/common.mk

SRCS = conf.c dialogs.c io.c ipc.c main.c tr_backend.c tr_torrent.c \
	tr_cell_renderer_progress.c util.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS  += $(CFLAGS_GTK) -I../libtransmission
LDFLAGS += $(LDFLAGS_GTK) ../libtransmission/libtransmission.a

all: transmission-gtk .po
	@true

transmission-gtk: $(OBJS) ../libtransmission/libtransmission.a
	$(LINK_RULE)

.po:
	@$(MAKE) -C po -f ../../mk/po.mk

%.o: %.c ../mk/config.mk ../mk/common.mk ../mk/gtk.mk
	$(CC_RULE)

clean:
	@echo "Clean transmission-gtk"
	@$(RM) transmission-gtk
	@echo "Clean $(OBJS)"
	@$(RM) $(OBJS)
	@$(MAKE) -C po -f ../../mk/po.mk clean

.depend: $(SRCS) ../mk/config.mk ../mk/common.mk ../mk/gtk.mk
	$(DEP_RULE)

install: transmission-gtk .po
	$(INSTALL_BIN_RULE)
	@$(MAKE) -C po -f ../../mk/po.mk install

morepot: $(SRCS)
	xgettext --output=po/transmission-gtk.pot --from-code=UTF-8 --add-comments --keyword=_ --keyword=N_ $^

-include .depend
