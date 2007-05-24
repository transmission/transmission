# $Id$

include ../mk/config.mk
include ../mk/common.mk

COMSRCS  = errors.c misc.c
SRVSRCS  = daemon.c server.c torrents.c
CLISRCS  = client.c remote.c
PXYSRCS  = proxy.c

COMOBJS  = $(COMSRCS:%.c=%.o)
SRVOBJS  = $(SRVSRCS:%.c=%.o)
CLIOBJS  = $(CLISRCS:%.c=%.o)
PXYOBJS  = $(PXYSRCS:%.c=%.o)
SRCS     = $(COMSRCS) $(SRVSRCS) $(CLISRCS) $(PXYSRCS)

CFLAGS  += $(CFLAGS_EVENT) -I../libtransmission
LDLIBS  += ../libtransmission/libtransmission.a
LDFLAGS += $(LDFLAGS_EVENT)

all: transmission-daemon transmission-remote transmission-proxy

transmission-daemon: OBJS    = $(SRVOBJS) $(COMOBJS)
transmission-daemon: $(LDLIBS) $(SRVOBJS) $(COMOBJS)
	$(LINK_RULE)

transmission-remote: OBJS    = $(CLIOBJS) $(COMOBJS)
transmission-remote: $(LDLIBS) $(CLIOBJS) $(COMOBJS)
	$(LINK_RULE)

transmission-proxy:  OBJS    = $(PXYOBJS) $(COMOBJS)
transmission-proxy:  $(LDLIBS) $(PXYOBJS) $(COMOBJS)
	$(LINK_RULE)

%.o: %.c ../mk/config.mk ../mk/common.mk ../mk/daemon.mk
	$(CC_RULE)

clean:
	@echo "Clean transmission-daemon"
	@echo "Clean transmission-remote"
	@echo "Clean transmission-proxy"
	@echo "Clean $(COMOBJS) $(SRVOBJS) $(CLIOBJS) $(PXYOBJS)"
	@$(RM) transmission-daemon transmission-remote
	@$(RM) $(COMOBJS) $(SRVOBJS) $(CLIOBJS) $(PXYOBJS)

.depend: $(SRCS) ../mk/config.mk ../mk/common.mk ../mk/daemon.mk
	$(DEP_RULE)

install: install.srv install.srv.man install.cli install.cli.man \
         install.pxy install.pxy.man

install.srv: transmission-daemon
	$(INSTALL_BIN_RULE)

install.srv.man: transmission-daemon.1
	$(INSTALL_MAN_RULE)

install.cli: transmission-remote
	$(INSTALL_BIN_RULE)

install.cli.man: transmission-remote.1
	$(INSTALL_MAN_RULE)

install.pxy: transmission-proxy
	$(INSTALL_BIN_RULE)

install.pxy.man: transmission-proxy.1
	$(INSTALL_MAN_RULE)

-include .depend
