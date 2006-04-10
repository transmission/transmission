include ../Makefile.config
include ../Makefile.common

SRCS = transmissioncli.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS  += -I../libtransmission
LDFLAGS += ../libtransmission/libtransmission.a

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

-include .depend
