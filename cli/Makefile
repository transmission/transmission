include ../Makefile.config
include ../Makefile.common

SRCS = transmissioncli.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS  += -I../libtransmission
LDFLAGS += ../libtransmission/libtransmission.a

all: transmissioncli
	@true

transmissioncli: $(OBJS) ../libtransmission/libtransmission.a
	$(LINK_RULE)

%.o: %.c ../Makefile.config ../Makefile.common Makefile
	$(CC_RULE)

clean:
	@echo "Clean transmissioncli"
	@$(RM) transmissioncli
	@echo "Clean $(OBJS)"
	@$(RM) $(OBJS)

.depend: $(SRCS) ../Makefile.config ../Makefile.common Makefile
	$(DEP_RULE)

install: install-bin install-man

install-bin: transmissioncli
	$(INSTALL_BIN_RULE)

install-man: transmissioncli.1
	$(INSTALL_MAN_RULE)

-include .depend
