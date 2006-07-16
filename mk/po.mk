# $Id$

include ../../mk/config.mk
include ../../mk/common.mk

POFILES = $(wildcard *.po)
MOFILES = $(POFILES:%.po=%.mo)

all: $(MOFILES)
	@true

%.mo: %.po ../../mk/config.mk ../../mk/common.mk ../../mk/po.mk
	$(MSGFMT_RULE)

%.mo.install: %.mo
	$(INSTALL_LOCALE_RULE)

clean:
	@echo "Clean $(MOFILES)"
	@$(RM) $(MOFILES)

install: $(MOFILES) $(MOFILES:%.mo=%.mo.install)

-include .depend
