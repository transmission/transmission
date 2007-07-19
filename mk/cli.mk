# $Id$

include ../mk/config.mk
include ../mk/common.mk

SRCS = transmissioncli.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS  += -I..
LDLIBS  += ../libtransmission/libtransmission.a

transmissioncli: $(OBJS) ../libtransmission/libtransmission.a
	$(LINK_RULE)

%.o: %.c ../mk/config.mk ../mk/common.mk ../mk/cli.mk
	$(CC_RULE)

clean:
	@echo "Clean transmissioncli"
	@$(RM) transmissioncli
	@echo "Clean $(OBJS)"
	@$(RM) $(OBJS)

.depend: $(SRCS) ../mk/config.mk ../mk/common.mk ../mk/cli.mk
	$(DEP_RULE)

install: install-bin install-man

install-bin: transmissioncli
	$(INSTALL_BIN_RULE)

install-man: transmissioncli.1
	$(INSTALL_MAN_RULE)

-include .depend
