include ../Makefile.config
include ../Makefile.common

SRCS = conf.c dialogs.c io.c ipc.c main.c tr_backend.c tr_torrent.c \
	tr_cell_renderer_torrent.c util.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS  += $(CFLAGS_GTK) -I../libtransmission
LDFLAGS += $(LDFLAGS_GTK) ../libtransmission/libtransmission.a

all: transmission-gtk .po
	@true

transmission-gtk: $(OBJS) ../libtransmission/libtransmission.a
	$(LINK_RULE)

.po:
	@$(MAKE) -C po

%.o: %.c ../Makefile.config ../Makefile.common Makefile
	$(CC_RULE)

clean:
	@echo "Clean transmission-gtk"
	@$(RM) transmission-gtk
	@echo "Clean $(OBJS)"
	@$(RM) $(OBJS)
	@$(MAKE) -C po clean

.depend: $(SRCS) ../Makefile.config ../Makefile.common Makefile
	$(DEP_RULE)

install: transmission-gtk .po
	$(INSTALL_BIN_RULE)
	@$(MAKE) -C po install

morepot: $(SRCS)
	xgettext --output=po/transmission-gtk.pot --from-code=UTF-8 --add-comments --keyword=_ --keyword=N_ $^

-include .depend
