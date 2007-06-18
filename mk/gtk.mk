# $Id$

include ../mk/config.mk
include ../mk/common.mk

SRCS = actions.c conf.c dialogs.c hig.c io.c ipc.c main.c msgwin.c \
       makemeta-ui.c torrent-inspector.c tr_cell_renderer_progress.c \
       tr_core.c tr_icon.c tr_prefs.c tr_torrent.c tr_window.c util.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS  += $(CFLAGS_GTK) -I../libtransmission
LDFLAGS += $(LDFLAGS_GTK)
LDLIBS  += ../libtransmission/libtransmission.a

all: transmission-gtk .po
	@true

transmission-gtk: $(OBJS) ../libtransmission/libtransmission.a
	$(LINK_RULE)

.po:
	@$(MAKE) $(MAKEARGS) -C po -f ../../mk/po.mk

%.o: %.c ../mk/config.mk ../mk/common.mk ../mk/gtk.mk
	$(CC_RULE)

clean:
	@echo "Clean transmission-gtk"
	@$(RM) transmission-gtk
	@echo "Clean $(OBJS)"
	@$(RM) $(OBJS)
	@$(MAKE) $(MAKEARGS) -C po -f ../../mk/po.mk clean

.depend: $(SRCS) ../mk/config.mk ../mk/common.mk ../mk/gtk.mk
	$(DEP_RULE)

install: transmission-gtk man.install desktop.install icon.install .po
	$(INSTALL_BIN_RULE)
	@$(MAKE) $(MAKEARGS) -C po -f ../../mk/po.mk install

desktop.install: transmission-gtk.desktop
	$(INSTALL_DESKTOP_RULE)

icon.install: transmission.png
	$(INSTALL_ICON_RULE)

man.install: transmission-gtk.1
	$(INSTALL_MAN_RULE)

morepot: $(SRCS)
	xgettext --output=po/transmission-gtk.pot --from-code=UTF-8 --add-comments --keyword=_ --keyword=N_ $^

-include .depend
